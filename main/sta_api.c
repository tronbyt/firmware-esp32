#include "sta_api.h"

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include "gfx.h"
#include "heap_monitor.h"
#include "version.h"
#include "wifi.h"

static const char* TAG = "sta_api";
static httpd_handle_t s_server = NULL;

static esp_err_t status_handler(httpd_req_t* req) {
  cJSON* root = cJSON_CreateObject();
  if (!root) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    return ESP_FAIL;
  }

  cJSON_AddStringToObject(root, "firmware_version", FIRMWARE_VERSION);

  uint8_t mac[6];
  if (wifi_get_mac(mac) == 0) {
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(root, "mac", mac_str);
  }

  heap_snapshot_t snap;
  heap_monitor_get_snapshot(&snap);
  cJSON_AddNumberToObject(root, "free_heap", (double)snap.internal_free);
  cJSON_AddNumberToObject(root, "free_spiram", (double)snap.spiram_free);
  cJSON_AddNumberToObject(root, "min_free_heap", (double)snap.internal_min);
  cJSON_AddNumberToObject(root, "images_loaded", gfx_get_loaded_counter());

  char* json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json);
  free(json);
  return ESP_OK;
}

static esp_err_t health_handler(httpd_req_t* req) {
  bool connected = wifi_is_connected();
  const char* resp =
      connected ? "{\"status\":\"ok\"}" : "{\"status\":\"degraded\"}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_status(req, connected ? "200 OK" : "503 Service Unavailable");
  httpd_resp_sendstr(req, resp);
  return ESP_OK;
}

esp_err_t sta_api_start(void) {
  if (s_server) {
    return ESP_OK;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 4;
  config.lru_purge_enable = true;

  esp_err_t err = httpd_start(&s_server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start STA API server: %s", esp_err_to_name(err));
    return err;
  }

  const httpd_uri_t status_uri = {
      .uri = "/api/status",
      .method = HTTP_GET,
      .handler = status_handler,
  };
  httpd_register_uri_handler(s_server, &status_uri);

  const httpd_uri_t health_uri = {
      .uri = "/api/health",
      .method = HTTP_GET,
      .handler = health_handler,
  };
  httpd_register_uri_handler(s_server, &health_uri);

  ESP_LOGI(TAG, "STA API server started on port %d", config.server_port);
  return ESP_OK;
}

esp_err_t sta_api_stop(void) {
  if (!s_server) {
    return ESP_OK;
  }
  esp_err_t err = httpd_stop(s_server);
  s_server = NULL;
  return err;
}
