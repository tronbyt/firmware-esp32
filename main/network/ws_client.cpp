#include "ws_client.h"

#include <cstdlib>
#include <cstring>

#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include "display.h"
#include "nvs_settings.h"
#include "ota.h"
#include "sdkconfig.h"
#include "syslog.h"
#include "version.h"
#include "webp_player.h"
#include "wifi.h"

namespace {

const char* TAG = "ws_client";

constexpr int WEBSOCKET_PROTOCOL_VERSION = 1;
constexpr EventBits_t WS_CONNECTED_BIT = BIT0;

#ifndef CONFIG_REFRESH_INTERVAL_SECONDS
constexpr int DEFAULT_REFRESH_INTERVAL = 10;
#else
constexpr int DEFAULT_REFRESH_INTERVAL = CONFIG_REFRESH_INTERVAL_SECONDS;
#endif

esp_websocket_client_handle_t s_ws_handle = nullptr;
EventGroupHandle_t s_ws_event_group = nullptr;

int32_t s_dwell_secs = DEFAULT_REFRESH_INTERVAL;
uint8_t* s_webp = nullptr;
size_t s_ws_accumulated_len = 0;
bool s_oversize_detected = false;
bool s_first_image_received = false;

void ota_task_entry(void* param) {
  auto* url = static_cast<char*>(param);
  run_ota(url);
  free(url);
  vTaskDelete(nullptr);
}

esp_err_t send_client_info() {
  esp_err_t ret = ESP_OK;
  uint8_t mac[6];
  char ssid[33] = {0};
  char hostname[33] = {0};
  char syslog_addr[MAX_SYSLOG_ADDR_LEN + 1] = {0};
  char sntp_server[MAX_SNTP_SERVER_LEN + 1] = {0};
  nvs_get_ssid(ssid, sizeof(ssid));
  nvs_get_hostname(hostname, sizeof(hostname));
  nvs_get_syslog_addr(syslog_addr, sizeof(syslog_addr));
  nvs_get_sntp_server(sntp_server, sizeof(sntp_server));
  const char* image_url = nvs_get_image_url();
  if (!image_url) image_url = "";

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

  cJSON_AddStringToObject(ci, "ssid", ssid);
  cJSON_AddStringToObject(ci, "hostname", hostname);
  cJSON_AddStringToObject(ci, "syslog_addr", syslog_addr);
  cJSON_AddStringToObject(ci, "sntp_server", sntp_server);
  cJSON_AddStringToObject(ci, "image_url", image_url);
  cJSON_AddBoolToObject(ci, "swap_colors", nvs_get_swap_colors());
  cJSON_AddNumberToObject(ci, "wifi_power_save", nvs_get_wifi_power_save());
  cJSON_AddBoolToObject(ci, "skip_display_version",
                        nvs_get_skip_display_version());
  cJSON_AddBoolToObject(ci, "ap_mode", nvs_get_ap_mode());
  cJSON_AddBoolToObject(ci, "prefer_ipv6", nvs_get_prefer_ipv6());

  char* json_str = cJSON_PrintUnformatted(root);
  if (json_str) {
    ESP_LOGI(TAG, "Sending client info: %s", json_str);
    int sent = esp_websocket_client_send_text(s_ws_handle, json_str,
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

void handle_text_message(esp_websocket_event_data_t* data) {
  bool is_complete =
      (data->payload_offset + data->data_len >= data->payload_len);
  if (!is_complete) return;

  auto* json_str = static_cast<char*>(malloc(data->data_len + 1));
  if (!json_str) {
    ESP_LOGE(TAG, "Failed to allocate memory for JSON parsing");
    return;
  }
  memcpy(json_str, data->data_ptr, data->data_len);
  json_str[data->data_len] = '\0';
  cJSON* root = cJSON_Parse(json_str);
  free(json_str);

  if (!root) {
    ESP_LOGW(TAG, "Failed to parse WebSocket text message as JSON");
    return;
  }

  bool settings_changed = false;

  cJSON* immediate_item = cJSON_GetObjectItem(root, "immediate");
  if (cJSON_IsBool(immediate_item) && cJSON_IsTrue(immediate_item)) {
    ESP_LOGD(TAG, "Interrupting current animation to load queued image");
    gfx_interrupt();
  }

  cJSON* dwell_item = cJSON_GetObjectItem(root, "dwell_secs");
  if (cJSON_IsNumber(dwell_item)) {
    int dwell_value = dwell_item->valueint;
    if (dwell_value < 1) dwell_value = 1;
    if (dwell_value > 3600) dwell_value = 3600;
    s_dwell_secs = dwell_value;
    ESP_LOGD(TAG, "Updated dwell_secs to %" PRId32 " seconds", s_dwell_secs);
  }

  cJSON* brightness_item = cJSON_GetObjectItem(root, "brightness");
  if (cJSON_IsNumber(brightness_item)) {
    int brightness_value = brightness_item->valueint;
    if (brightness_value < DISPLAY_MIN_BRIGHTNESS)
      brightness_value = DISPLAY_MIN_BRIGHTNESS;
    if (brightness_value > DISPLAY_MAX_BRIGHTNESS)
      brightness_value = DISPLAY_MAX_BRIGHTNESS;
    display_set_brightness(static_cast<uint8_t>(brightness_value));
    ESP_LOGI(TAG, "Updated brightness to %d", brightness_value);
  }

  cJSON* ota_item = cJSON_GetObjectItem(root, "ota_url");
  if (cJSON_IsString(ota_item) && ota_item->valuestring) {
    char* ota_url = strdup(ota_item->valuestring);
    if (ota_url) {
      ESP_LOGI(TAG, "OTA URL received via WS: %s", ota_url);
      xTaskCreate(ota_task_entry, "ota_task", 8192, ota_url, 5, nullptr);
    }
  }

  cJSON* swap_colors_item = cJSON_GetObjectItem(root, "swap_colors");
  if (cJSON_IsBool(swap_colors_item)) {
    bool val = cJSON_IsTrue(swap_colors_item);
    nvs_set_swap_colors(val);
    ESP_LOGI(TAG, "Updated swap_colors to %d", val);
    settings_changed = true;
  }

  cJSON* wifi_ps_item = cJSON_GetObjectItem(root, "wifi_power_save");
  if (cJSON_IsNumber(wifi_ps_item)) {
    auto val = static_cast<wifi_ps_type_t>(wifi_ps_item->valueint);
    nvs_set_wifi_power_save(val);
    ESP_LOGI(TAG, "Updated wifi_power_save to %d", val);
    settings_changed = true;
    wifi_apply_power_save();
  }

  cJSON* skip_ver_item = cJSON_GetObjectItem(root, "skip_display_version");
  if (cJSON_IsBool(skip_ver_item)) {
    bool val = cJSON_IsTrue(skip_ver_item);
    nvs_set_skip_display_version(val);
    ESP_LOGI(TAG, "Updated skip_display_version to %d", val);
    settings_changed = true;
  }

  cJSON* ap_mode_item = cJSON_GetObjectItem(root, "ap_mode");
  if (cJSON_IsBool(ap_mode_item)) {
    bool val = cJSON_IsTrue(ap_mode_item);
    nvs_set_ap_mode(val);
    ESP_LOGI(TAG, "Updated ap_mode to %d", val);
    settings_changed = true;
  }

  cJSON* prefer_ipv6_item = cJSON_GetObjectItem(root, "prefer_ipv6");
  if (cJSON_IsBool(prefer_ipv6_item)) {
    bool val = cJSON_IsTrue(prefer_ipv6_item);
    nvs_set_prefer_ipv6(val);
    ESP_LOGI(TAG, "Updated prefer_ipv6 to %d", val);
    settings_changed = true;
  }

  cJSON* hostname_item = cJSON_GetObjectItem(root, "hostname");
  if (cJSON_IsString(hostname_item) && hostname_item->valuestring) {
    const char* new_hostname = hostname_item->valuestring;
    if (strlen(new_hostname) > 0 && strlen(new_hostname) <= 32) {
      nvs_set_hostname(new_hostname);
      wifi_set_hostname(new_hostname);
      ESP_LOGI(TAG, "Updated hostname to %s", new_hostname);
      settings_changed = true;
    } else {
      ESP_LOGW(TAG, "Invalid hostname received: %s", new_hostname);
    }
  }

  cJSON* syslog_addr_item = cJSON_GetObjectItem(root, "syslog_addr");
  if (cJSON_IsString(syslog_addr_item) && syslog_addr_item->valuestring) {
    const char* new_addr = syslog_addr_item->valuestring;
    nvs_set_syslog_addr(new_addr);
    syslog_update_config(new_addr);
    ESP_LOGI(TAG, "Updated syslog_addr to %s", new_addr);
    settings_changed = true;
  }

  cJSON* sntp_server_item = cJSON_GetObjectItem(root, "sntp_server");
  if (cJSON_IsString(sntp_server_item) && sntp_server_item->valuestring) {
    const char* new_server = sntp_server_item->valuestring;
    nvs_set_sntp_server(new_server);
    ESP_LOGI(TAG, "Updated sntp_server to %s", new_server);
    settings_changed = true;
  }

  cJSON* image_url_item = cJSON_GetObjectItem(root, "image_url");
  if (cJSON_IsString(image_url_item) && image_url_item->valuestring) {
    nvs_set_image_url(image_url_item->valuestring);
    ESP_LOGI(TAG, "Updated image_url to %s", image_url_item->valuestring);
    settings_changed = true;
  }

  if (settings_changed) {
    esp_err_t err = nvs_save_settings();
    if (err == ESP_OK) {
      send_client_info();
    } else {
      ESP_LOGE(TAG, "Failed to save settings: %s", esp_err_to_name(err));
    }
  }

  cJSON* reboot_item = cJSON_GetObjectItem(root, "reboot");
  if (cJSON_IsBool(reboot_item) && cJSON_IsTrue(reboot_item)) {
    ESP_LOGI(TAG, "Reboot command received via WS");
    cJSON_Delete(root);
    esp_restart();
  }

  cJSON_Delete(root);
}

void handle_binary_message(esp_websocket_event_data_t* data) {
  if (data->op_code == 2 && data->payload_offset == 0) {
    if (s_webp) {
      ESP_LOGW(TAG, "Discarding incomplete previous WebP buffer");
      free(s_webp);
      s_webp = nullptr;
    }
    s_ws_accumulated_len = 0;
    s_oversize_detected = false;
  }

  if (s_oversize_detected) return;

  if (data->op_code == 0 && !s_webp) return;

  size_t new_size = s_ws_accumulated_len + data->data_len;
  if (new_size > CONFIG_HTTP_BUFFER_SIZE_MAX) {
    ESP_LOGE(TAG, "WebP size (%zu bytes) exceeds max (%d)", new_size,
             CONFIG_HTTP_BUFFER_SIZE_MAX);
    s_oversize_detected = true;
    if (gfx_display_asset("oversize") != 0) {
      ESP_LOGE(TAG, "Failed to display oversize graphic");
    }
    free(s_webp);
    s_webp = nullptr;
    s_ws_accumulated_len = 0;
    return;
  }

  auto* new_buf = static_cast<uint8_t*>(
      heap_caps_realloc(s_webp, new_size, MALLOC_CAP_SPIRAM));
  if (!new_buf) {
    ESP_LOGE(TAG, "Failed to allocate memory (%zu bytes)", new_size);
    free(s_webp);
    s_webp = nullptr;
    s_ws_accumulated_len = 0;
    return;
  }
  s_webp = new_buf;

  memcpy(s_webp + s_ws_accumulated_len, data->data_ptr, data->data_len);
  s_ws_accumulated_len = new_size;

  bool frame_complete =
      (data->payload_offset + data->data_len >= data->payload_len);

  if (data->fin && frame_complete) {
    ESP_LOGD(TAG, "WebP download complete (%zu bytes)", s_ws_accumulated_len);

    gfx_update(s_webp, s_ws_accumulated_len, s_dwell_secs);

    if (!s_first_image_received) {
      ESP_LOGI(TAG,
               "First WebSocket image received - interrupting boot animation");
      gfx_interrupt();
      s_first_image_received = true;
    }

    // Ownership transferred to gfx
    s_webp = nullptr;
    s_ws_accumulated_len = 0;
  }
}

void websocket_event_handler(void* handler_args, esp_event_base_t base,
                             int32_t event_id, void* event_data) {
  auto* data = static_cast<esp_websocket_event_data_t*>(event_data);
  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
      xEventGroupSetBits(s_ws_event_group, WS_CONNECTED_BIT);
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
      draw_error_indicator_pixel();
      break;
    case WEBSOCKET_EVENT_DATA:
      if (data->op_code == 1 && data->data_len > 0) {
        handle_text_message(data);
      } else if (data->op_code == 2 || data->op_code == 0) {
        handle_binary_message(data);
      }
      break;
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGE(TAG, "WEBSOCKET_EVENT_ERROR");
      if (s_webp) {
        ESP_LOGW(TAG,
                 "WebSocket error with incomplete WebP buffer - discarding");
        free(s_webp);
        s_webp = nullptr;
      }
      draw_error_indicator_pixel();
      break;
  }
}

}  // namespace

esp_err_t ws_client_start(const char* url) {
  ESP_LOGI(TAG, "Starting WebSocket client with URL: %s", url);

  const esp_websocket_client_config_t ws_cfg = {
      .uri = url,
      .task_stack = 8192,
      .buffer_size = 8192,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .reconnect_timeout_ms = 10000,
      .network_timeout_ms = 10000,
  };

  s_ws_handle = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(s_ws_handle, WEBSOCKET_EVENT_ANY,
                                websocket_event_handler, s_ws_handle);

  gfx_set_websocket_handle(s_ws_handle);

  s_ws_event_group = xEventGroupCreate();

  esp_err_t err = esp_websocket_client_start(s_ws_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start WebSocket client: %d", err);
    return err;
  }

  xEventGroupWaitBits(s_ws_event_group, WS_CONNECTED_BIT, pdFALSE, pdTRUE,
                      pdMS_TO_TICKS(5000));

  return ESP_OK;
}

void ws_client_stop(void) {
  if (s_ws_handle) {
    esp_websocket_client_stop(s_ws_handle);
    esp_websocket_client_destroy(s_ws_handle);
    s_ws_handle = nullptr;
  }
  if (s_ws_event_group) {
    vEventGroupDelete(s_ws_event_group);
    s_ws_event_group = nullptr;
  }
}

void ws_client_run_loop(void) {
  bool was_connected = false;

  while (true) {
    bool is_connected = esp_websocket_client_is_connected(s_ws_handle);

    if (is_connected) {
      if (!was_connected) {
        ESP_LOGI(TAG, "WebSocket connected, sending client info");
        send_client_info();
        was_connected = true;
      }
    } else {
      if (was_connected) {
        was_connected = false;
        ESP_LOGW(TAG, "WebSocket disconnected");
      }
      ESP_LOGW(TAG, "WebSocket not connected. Attempting to reconnect...");
      esp_websocket_client_stop(s_ws_handle);
      esp_err_t err = esp_websocket_client_start(s_ws_handle);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Reconnection failed with error %d", err);
      }
    }

    wifi_health_check();
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
