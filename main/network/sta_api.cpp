#include "sta_api.h"

#include <cstring>

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "embedded_tz_db.h"
#include "heap_monitor.h"
#include "http_server.h"
#include "mdns_service.h"
#include "ntp.h"
#include "nvs_settings.h"
#include "version.h"
#include "webp_player.h"
#include "wifi.h"

namespace {

const char* TAG = "sta_api";

// ── Existing endpoints ─────────────────────────────────────────────

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

// ── New endpoints (ported from kd_common) ──────────────────────────

esp_err_t about_handler(httpd_req_t* req) {
  const esp_app_desc_t* app = esp_app_get_description();

  cJSON* root = cJSON_CreateObject();
  if (!root) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    return ESP_FAIL;
  }

  cJSON_AddStringToObject(root, "model", mdns_board_model());
  cJSON_AddStringToObject(root, "type", "tronbyt");
  cJSON_AddStringToObject(root, "version", app->version);

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

esp_err_t system_config_get_handler(httpd_req_t* req) {
  auto cfg = config_get();

  cJSON* root = cJSON_CreateObject();
  if (!root) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    return ESP_FAIL;
  }

  cJSON_AddBoolToObject(root, "auto_timezone", ntp_get_auto_timezone());
  cJSON_AddStringToObject(root, "timezone", ntp_get_timezone());
  cJSON_AddStringToObject(root, "ntp_server", ntp_get_server());
  cJSON_AddStringToObject(root, "hostname", cfg.hostname);

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

esp_err_t system_config_post_handler(httpd_req_t* req) {
  char content[512];
  int ret = httpd_req_recv(req, content, sizeof(content) - 1);
  if (ret <= 0) {
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
      httpd_resp_send_408(req);
    } else {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "receive error");
    }
    return ESP_FAIL;
  }
  content[ret] = '\0';

  cJSON* json = cJSON_Parse(content);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON* auto_tz = cJSON_GetObjectItem(json, "auto_timezone");
  cJSON* tz = cJSON_GetObjectItem(json, "timezone");
  cJSON* ntp = cJSON_GetObjectItem(json, "ntp_server");
  cJSON* hostname = cJSON_GetObjectItem(json, "hostname");

  if (cJSON_IsBool(auto_tz)) {
    ntp_set_auto_timezone(cJSON_IsTrue(auto_tz));
  }

  if (cJSON_IsString(tz)) {
    const char* val = cJSON_GetStringValue(tz);
    if (val && strlen(val) < 64) {
      ntp_set_timezone(val);
    }
  }

  if (cJSON_IsString(ntp)) {
    const char* val = cJSON_GetStringValue(ntp);
    if (val && strlen(val) < 64) {
      ntp_set_server(val);
    }
  }

  if (cJSON_IsString(hostname)) {
    const char* val = cJSON_GetStringValue(hostname);
    if (val && strlen(val) > 0 && strlen(val) <= MAX_HOSTNAME_LEN) {
      auto cfg = config_get();
      strncpy(cfg.hostname, val, MAX_HOSTNAME_LEN);
      cfg.hostname[MAX_HOSTNAME_LEN] = '\0';
      config_set(&cfg);
      wifi_set_hostname(val);
    }
  }

  cJSON_Delete(json);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"success\"}");
  return ESP_OK;
}

esp_err_t time_zonedb_handler(httpd_req_t* req) {
  const embeddedTz_t* zones = tz_db_get_all_zones();
  int total = TZ_DB_NUM_ZONES;

  httpd_resp_set_type(req, "application/json");

  httpd_resp_send_chunk(req, "[", 1);

  constexpr int CHUNK_SIZE = 20;
  bool first = true;

  for (int start = 0; start < total; start += CHUNK_SIZE) {
    int end = start + CHUNK_SIZE;
    if (end > total) end = total;

    cJSON* arr = cJSON_CreateArray();
    if (!arr) {
      httpd_resp_send_chunk(req, nullptr, 0);
      return ESP_FAIL;
    }

    for (int i = start; i < end; i++) {
      cJSON* obj = cJSON_CreateObject();
      if (!obj) continue;
      cJSON_AddStringToObject(obj, "name", zones[i].name);
      cJSON_AddStringToObject(obj, "rule", zones[i].rule);
      cJSON_AddItemToArray(arr, obj);
    }

    char* chunk_str = cJSON_PrintUnformatted(arr);
    if (!chunk_str) {
      cJSON_Delete(arr);
      httpd_resp_send_chunk(req, nullptr, 0);
      return ESP_FAIL;
    }

    size_t len = strlen(chunk_str);
    if (len > 2) {
      // Strip outer [ ] — send inner content only
      chunk_str[len - 1] = '\0';
      char* inner = chunk_str + 1;

      if (!first) {
        httpd_resp_send_chunk(req, ",", 1);
      }
      httpd_resp_send_chunk(req, inner, strlen(inner));
      first = false;
    }

    free(chunk_str);
    cJSON_Delete(arr);

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  httpd_resp_send_chunk(req, "]", 1);
  httpd_resp_send_chunk(req, nullptr, 0);

  return ESP_OK;
}

}  // namespace

esp_err_t sta_api_start(void) {
  httpd_handle_t server = http_server_handle();
  if (!server) {
    ESP_LOGE(TAG, "HTTP server not running");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Registering API endpoints on central HTTP server");

  const httpd_uri_t status_uri = {
      .uri = "/api/status",
      .method = HTTP_GET,
      .handler = status_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &status_uri);

  const httpd_uri_t health_uri = {
      .uri = "/api/health",
      .method = HTTP_GET,
      .handler = health_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &health_uri);

  const httpd_uri_t about_uri = {
      .uri = "/api/about",
      .method = HTTP_GET,
      .handler = about_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &about_uri);

  const httpd_uri_t sys_config_get_uri = {
      .uri = "/api/system/config",
      .method = HTTP_GET,
      .handler = system_config_get_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &sys_config_get_uri);

  const httpd_uri_t sys_config_post_uri = {
      .uri = "/api/system/config",
      .method = HTTP_POST,
      .handler = system_config_post_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &sys_config_post_uri);

  const httpd_uri_t zonedb_uri = {
      .uri = "/api/time/zonedb",
      .method = HTTP_GET,
      .handler = time_zonedb_handler,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &zonedb_uri);

  return ESP_OK;
}
