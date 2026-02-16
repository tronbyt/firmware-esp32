#include "handlers.h"

#include <cstdlib>
#include <cstring>

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "display.h"
#include "messages.h"
#include "nvs_settings.h"
#include "ota.h"
#include "sdkconfig.h"
#include "syslog.h"
#include "webp_player.h"
#include "wifi.h"

namespace {

const char* TAG = "handlers";

#ifndef CONFIG_REFRESH_INTERVAL_SECONDS
constexpr int DEFAULT_REFRESH_INTERVAL = 10;
#else
constexpr int DEFAULT_REFRESH_INTERVAL = CONFIG_REFRESH_INTERVAL_SECONDS;
#endif

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

}  // namespace

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
      msg_send_client_info();
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
