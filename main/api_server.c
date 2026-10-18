#include "api_server.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <stdlib.h>
#include <string.h>

#include "app_api.h"
#include "apps.h"
#include "nvs_settings.h"
#include "version.h"

static const char *TAG = "api";

#define MAX_JSON_BODY (32 * 1024)
#define OTA_CHUNK 4096

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t s_server;

// --- helpers --------------------------------------------------------------------

static esp_err_t send_json(httpd_req_t *req, const char *status, const char *json) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, json);
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "error", msg);
  char *s = cJSON_PrintUnformatted(o);
  esp_err_t err = send_json(req, status, s);
  free(s);
  cJSON_Delete(o);
  return err;
}

static esp_err_t send_ok(httpd_req_t *req) {
  return send_json(req, "200 OK", "{\"ok\":true}");
}

// Read the request body (NUL-terminated, in PSRAM). NULL on error/too large.
static char *read_body(httpd_req_t *req) {
  if (req->content_len > MAX_JSON_BODY) return NULL;
  char *buf = heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM);
  if (!buf) return NULL;
  size_t got = 0;
  while (got < req->content_len) {
    int r = httpd_req_recv(req, buf + got, req->content_len - got);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (r <= 0) {
      free(buf);
      return NULL;
    }
    got += r;
  }
  buf[got] = '\0';
  return buf;
}

static cJSON *read_json(httpd_req_t *req) {
  char *body = read_body(req);
  if (!body) return NULL;
  cJSON *root = cJSON_Parse(body);
  free(body);
  return root;
}

static void reboot_task(void *arg) {
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
}

static void reboot_soon(void) { xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL); }

// --- handlers -------------------------------------------------------------------

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t status_handler(httpd_req_t *req) {
  cJSON *o = cJSON_CreateObject();
  const char *image_url = nvs_get_image_url();
  cJSON_AddStringToObject(o, "firmware", FIRMWARE_VERSION);
  cJSON_AddStringToObject(o, "mode", apps_local_mode(image_url) ? "local" : "remote");
  cJSON_AddStringToObject(o, "image_url", image_url ? image_url : "");
  cJSON_AddStringToObject(o, "current_app", apps_current());
  cJSON_AddNumberToObject(o, "uptime_s", esp_timer_get_time() / 1000000);
  cJSON_AddNumberToObject(o, "free_heap", esp_get_free_heap_size());
  cJSON_AddNumberToObject(o, "free_internal", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  cJSON_AddNumberToObject(o, "free_psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  cJSON_AddBoolToObject(o, "time_valid", time_is_valid());
  cJSON_AddBoolToObject(o, "spotify_configured", spotify_is_configured());

  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    cJSON_AddStringToObject(o, "ssid", (const char *)ap.ssid);
    cJSON_AddNumberToObject(o, "rssi", ap.rssi);
  }
  esp_netif_ip_info_t ip;
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
    char buf[16];
    esp_ip4addr_ntoa(&ip.ip, buf, sizeof(buf));
    cJSON_AddStringToObject(o, "ip", buf);
  }
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running) cJSON_AddStringToObject(o, "partition", running->label);

  char *s = cJSON_PrintUnformatted(o);
  esp_err_t err = send_json(req, "200 OK", s);
  free(s);
  cJSON_Delete(o);
  return err;
}

static esp_err_t apps_handler(httpd_req_t *req) {
  char *s = apps_list_json();
  esp_err_t err = send_json(req, "200 OK", s);
  free(s);
  return err;
}

static esp_err_t config_get_handler(httpd_req_t *req) {
  char *s = apps_get_config_json();
  esp_err_t err = send_json(req, "200 OK", s);
  free(s);
  return err;
}

static esp_err_t config_put_handler(httpd_req_t *req) {
  char *body = read_body(req);
  if (!body) return send_error(req, "400 Bad Request", "missing or too large body");
  const char *error = "invalid config";
  esp_err_t err = apps_set_config_json(body, &error);
  free(body);
  if (err != ESP_OK) return send_error(req, "400 Bad Request", error);
  return send_ok(req);
}

static esp_err_t notify_handler(httpd_req_t *req) {
  cJSON *root = read_json(req);
  const char *text = cfg_str(root, "text", "");
  if (!text[0]) {
    cJSON_Delete(root);
    return send_error(req, "400 Bad Request", "text is required");
  }
  esp_err_t err = apps_notify(cfg_str(root, "title", ""), text,
                              cfg_str(root, "color", ""), cfg_int(root, "secs", 15));
  cJSON_Delete(root);
  if (err == ESP_ERR_INVALID_STATE)
    return send_error(req, "409 Conflict", "notifications need local mode");
  if (err != ESP_OK) return send_error(req, "503 Service Unavailable", "queue full");
  return send_ok(req);
}

static esp_err_t next_handler(httpd_req_t *req) {
  apps_next();
  return send_ok(req);
}

static esp_err_t spotify_post_handler(httpd_req_t *req) {
  cJSON *root = read_json(req);
  const char *client_id = cfg_str(root, "client_id", "");
  const char *refresh_token = cfg_str(root, "refresh_token", "");
  if (!client_id[0] || !refresh_token[0]) {
    cJSON_Delete(root);
    return send_error(req, "400 Bad Request", "client_id and refresh_token are required");
  }
  spotify_set_credentials(client_id, refresh_token);
  cJSON_Delete(root);
  return send_ok(req);
}

static esp_err_t spotify_delete_handler(httpd_req_t *req) {
  spotify_set_credentials("", "");
  return send_ok(req);
}

static esp_err_t mode_handler(httpd_req_t *req) {
  cJSON *root = read_json(req);
  const char *url = cfg_str(root, "image_url", NULL);
  if (!url || strlen(url) > MAX_URL_LEN) {
    cJSON_Delete(root);
    return send_error(req, "400 Bad Request", "image_url is required (\"local\" or a URL)");
  }
  nvs_set_image_url(url);
  esp_err_t err = nvs_save_settings();
  cJSON_Delete(root);
  if (err != ESP_OK) return send_error(req, "500 Internal Server Error", "could not save");
  send_ok(req);
  reboot_soon();
  return ESP_OK;
}

static esp_err_t reboot_handler(httpd_req_t *req) {
  send_ok(req);
  reboot_soon();
  return ESP_OK;
}

// Firmware update over WiFi. esp_ota_end() verifies the image (format and
// SHA-256) before we switch the boot partition, so a truncated or corrupt
// upload never becomes bootable.
static esp_err_t ota_handler(httpd_req_t *req) {
  const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
  if (!target) return send_error(req, "500 Internal Server Error", "no OTA partition");
  if (req->content_len < 64 * 1024 || req->content_len > target->size)
    return send_error(req, "400 Bad Request", "body must be a firmware .bin");

  char *buf = heap_caps_malloc(OTA_CHUNK, MALLOC_CAP_SPIRAM);
  if (!buf) return send_error(req, "500 Internal Server Error", "out of memory");

  esp_ota_handle_t ota;
  esp_err_t err = esp_ota_begin(target, req->content_len, &ota);
  if (err != ESP_OK) {
    free(buf);
    return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
  }
  ESP_LOGI(TAG, "OTA: writing %d bytes to %s", req->content_len, target->label);

  size_t remaining = req->content_len;
  bool first = true;
  while (remaining > 0) {
    int r = httpd_req_recv(req, buf, remaining < OTA_CHUNK ? remaining : OTA_CHUNK);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (r <= 0) {
      err = ESP_FAIL;
      break;
    }
    if (first) {
      first = false;
      if ((uint8_t)buf[0] != 0xE9) {  // ESP image magic byte
        err = ESP_ERR_OTA_VALIDATE_FAILED;
        break;
      }
    }
    err = esp_ota_write(ota, buf, r);
    if (err != ESP_OK) break;
    remaining -= r;
  }
  free(buf);
  if (err != ESP_OK) {
    esp_ota_abort(ota);
    ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    return send_error(req, "400 Bad Request", esp_err_to_name(err));
  }
  err = esp_ota_end(ota);  // validates the image
  if (err == ESP_OK) err = esp_ota_set_boot_partition(target);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "OTA validation failed: %s", esp_err_to_name(err));
    return send_error(req, "400 Bad Request", esp_err_to_name(err));
  }
  ESP_LOGI(TAG, "OTA ok, rebooting into %s", target->label);
  send_ok(req);
  reboot_soon();
  return ESP_OK;
}

esp_err_t api_server_start(void) {
  if (s_server) return ESP_OK;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 8080;
  config.ctrl_port = 32769;  // the AP portal's server uses the default
  config.max_uri_handlers = 20;
  config.max_open_sockets = 4;
  config.stack_size = 8192;
  config.recv_wait_timeout = 15;
  config.send_wait_timeout = 15;
  config.lru_purge_enable = true;
  config.uri_match_fn = httpd_uri_match_wildcard;

  esp_err_t err = httpd_start(&s_server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "could not start API server: %s", esp_err_to_name(err));
    return err;
  }
  const httpd_uri_t routes[] = {
      {"/", HTTP_GET, index_handler, NULL},
      {"/api/status", HTTP_GET, status_handler, NULL},
      {"/api/apps", HTTP_GET, apps_handler, NULL},
      {"/api/config", HTTP_GET, config_get_handler, NULL},
      {"/api/config", HTTP_PUT, config_put_handler, NULL},
      {"/api/notify", HTTP_POST, notify_handler, NULL},
      {"/api/next", HTTP_POST, next_handler, NULL},
      {"/api/spotify", HTTP_POST, spotify_post_handler, NULL},
      {"/api/spotify", HTTP_DELETE, spotify_delete_handler, NULL},
      {"/api/mode", HTTP_POST, mode_handler, NULL},
      {"/api/ota", HTTP_POST, ota_handler, NULL},
      {"/api/reboot", HTTP_POST, reboot_handler, NULL},
  };
  for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
    httpd_register_uri_handler(s_server, &routes[i]);
  }
  ESP_LOGI(TAG, "API server listening on port %d", config.server_port);
  return ESP_OK;
}
