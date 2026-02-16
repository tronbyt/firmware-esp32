#include "messages.h"

#include <cstring>

#include <cJSON.h>
#include <esp_log.h>

#include "nvs_settings.h"
#include "version.h"
#include "wifi.h"

namespace {

const char* TAG = "messages";

constexpr int WEBSOCKET_PROTOCOL_VERSION = 1;

esp_websocket_client_handle_t s_client = nullptr;

}  // namespace

void msg_init(esp_websocket_client_handle_t client) {
  s_client = client;
}

esp_err_t msg_send_client_info() {
  if (!s_client) return ESP_ERR_INVALID_STATE;

  esp_err_t ret = ESP_OK;
  uint8_t mac[6];
  auto cfg = config_get();

  cJSON* root = cJSON_CreateObject();
  if (!root) return ESP_ERR_NO_MEM;

  cJSON* ci = cJSON_AddObjectToObject(root, "client_info");
  if (!ci) {
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  cJSON_AddStringToObject(ci, "firmware_version", FIRMWARE_VERSION);
  cJSON_AddStringToObject(ci, "firmware_type", "ESP32");
  cJSON_AddNumberToObject(ci, "protocol_version", WEBSOCKET_PROTOCOL_VERSION);

  if (wifi_get_mac(mac) == 0) {
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(ci, "mac", mac_str);
  } else {
    ESP_LOGW(TAG,
             "Failed to get MAC address; sending client info without MAC.");
  }

  cJSON_AddStringToObject(ci, "ssid", cfg.ssid);
  cJSON_AddStringToObject(ci, "hostname", cfg.hostname);
  cJSON_AddStringToObject(ci, "syslog_addr", cfg.syslog_addr);
  cJSON_AddStringToObject(ci, "sntp_server", cfg.sntp_server);
  cJSON_AddStringToObject(ci, "image_url", cfg.image_url);
  cJSON_AddBoolToObject(ci, "swap_colors", cfg.swap_colors);
  cJSON_AddNumberToObject(ci, "wifi_power_save", cfg.wifi_power_save);
  cJSON_AddBoolToObject(ci, "skip_display_version",
                        cfg.skip_display_version);
  cJSON_AddBoolToObject(ci, "ap_mode", cfg.ap_mode);
  cJSON_AddBoolToObject(ci, "prefer_ipv6", cfg.prefer_ipv6);

  char* json_str = cJSON_PrintUnformatted(root);
  if (json_str) {
    ESP_LOGI(TAG, "Sending client info: %s", json_str);
    int sent = esp_websocket_client_send_text(s_client, json_str,
                                              strlen(json_str), portMAX_DELAY);
    if (sent < 0) {
      ESP_LOGE(TAG, "Failed to send client info: %d", sent);
      ret = ESP_FAIL;
    }
    free(json_str);
  } else {
    ret = ESP_ERR_NO_MEM;
  }

  cJSON_Delete(root);
  return ret;
}
