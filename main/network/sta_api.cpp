#include "sta_api.h"

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include "ap.h"
#include "webp_player.h"
#include "heap_monitor.h"
#include "version.h"
#include "wifi.h"

namespace {

const char* TAG = "sta_api";
httpd_handle_t s_server = nullptr;
bool s_owns_server = false;

esp_err_t status_handler(httpd_req_t* req) {
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
  cJSON_AddNumberToObject(root, "free_heap",
                          static_cast<double>(snap.internal_free));
  cJSON_AddNumberToObject(root, "free_spiram",
                          static_cast<double>(snap.spiram_free));
  cJSON_AddNumberToObject(root, "min_free_heap",
                          static_cast<double>(snap.internal_min));
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

esp_err_t health_handler(httpd_req_t* req) {
  bool connected = wifi_is_connected();
  const char* resp =
      connected ? "{\"status\":\"ok\"}" : "{\"status\":\"degraded\"}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_status(req, connected ? "200 OK" : "503 Service Unavailable");
  httpd_resp_sendstr(req, resp);
  return ESP_OK;
}

}  // namespace

esp_err_t sta_api_start(void) {
  if (s_server) {
    return ESP_OK;
  }

  // If the AP web server is already running, register endpoints on it
  // instead of starting a second server (both would compete for port 80).
  httpd_handle_t ap_server = ap_get_server();
  if (ap_server) {
    s_server = ap_server;
    s_owns_server = false;
    ESP_LOGI(TAG, "Registering API endpoints on existing AP server");
  } else {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to start STA API server: %s",
               esp_err_to_name(err));
      return err;
    }
    s_owns_server = true;
    ESP_LOGI(TAG, "STA API server started on port %d", config.server_port);
  }

  const httpd_uri_t status_uri = {
      .uri = "/api/status",
      .method = HTTP_GET,
      .handler = status_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(s_server, &status_uri);

  const httpd_uri_t health_uri = {
      .uri = "/api/health",
      .method = HTTP_GET,
      .handler = health_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(s_server, &health_uri);

  return ESP_OK;
}

esp_err_t sta_api_stop(void) {
  if (!s_server) {
    return ESP_OK;
  }
  esp_err_t err = ESP_OK;
  if (s_owns_server) {
    err = httpd_stop(s_server);
  }
  s_server = nullptr;
  s_owns_server = false;
  return err;
}

bool sta_api_owns_server(httpd_handle_t server) {
  return s_server != nullptr && s_server == server;
}
