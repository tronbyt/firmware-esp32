#include "handlers.h"

#include <cstdlib>
#include <cstring>

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
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

constexpr int TEXT_QUEUE_DEPTH = 4;
constexpr int CONSUMER_STACK_SIZE = 6144;
constexpr int CONSUMER_PRIORITY = 4;

struct TextMsg {
  char* data;
  size_t len;
};

int32_t s_dwell_secs = DEFAULT_REFRESH_INTERVAL;
uint8_t* s_webp = nullptr;
size_t s_ws_accumulated_len = 0;
bool s_oversize_detected = false;
bool s_first_image_received = false;

QueueHandle_t s_text_queue = nullptr;
TaskHandle_t s_consumer_task = nullptr;

void ota_task_entry(void* param) {
  auto* url = static_cast<char*>(param);
  run_ota(url);
  free(url);
  vTaskDelete(nullptr);
}

void process_text_message(const char* json_str) {
  cJSON* root = cJSON_Parse(json_str);

  if (!root) {
    ESP_LOGW(TAG, "Failed to parse WebSocket text message as JSON");
    return;
  }

  bool settings_changed = false;
  auto cfg = config_get();

  cJSON* immediate_item = cJSON_GetObjectItem(root, "immediate");
  if (cJSON_IsBool(immediate_item) && cJSON_IsTrue(immediate_item)) {
    ESP_LOGD(TAG, "Interrupting current animation to load queued image");
    gfx_preempt();
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
    cfg.swap_colors = val;
    ESP_LOGI(TAG, "Updated swap_colors to %d", val);
    settings_changed = true;
  }

  cJSON* wifi_ps_item = cJSON_GetObjectItem(root, "wifi_power_save");
  if (cJSON_IsNumber(wifi_ps_item)) {
    auto val = static_cast<wifi_ps_type_t>(wifi_ps_item->valueint);
    cfg.wifi_power_save = val;
    ESP_LOGI(TAG, "Updated wifi_power_save to %d", val);
    settings_changed = true;
    wifi_apply_power_save();
  }

  cJSON* skip_ver_item = cJSON_GetObjectItem(root, "skip_display_version");
  if (cJSON_IsBool(skip_ver_item)) {
    bool val = cJSON_IsTrue(skip_ver_item);
    cfg.skip_display_version = val;
    ESP_LOGI(TAG, "Updated skip_display_version to %d", val);
    settings_changed = true;
  }

  cJSON* ap_mode_item = cJSON_GetObjectItem(root, "ap_mode");
  if (cJSON_IsBool(ap_mode_item)) {
    bool val = cJSON_IsTrue(ap_mode_item);
    cfg.ap_mode = val;
    ESP_LOGI(TAG, "Updated ap_mode to %d", val);
    settings_changed = true;
  }

  cJSON* prefer_ipv6_item = cJSON_GetObjectItem(root, "prefer_ipv6");
  if (cJSON_IsBool(prefer_ipv6_item)) {
    bool val = cJSON_IsTrue(prefer_ipv6_item);
    cfg.prefer_ipv6 = val;
    ESP_LOGI(TAG, "Updated prefer_ipv6 to %d", val);
    settings_changed = true;
  }

  cJSON* hostname_item = cJSON_GetObjectItem(root, "hostname");
  if (cJSON_IsString(hostname_item) && hostname_item->valuestring) {
    const char* new_hostname = hostname_item->valuestring;
    if (strlen(new_hostname) > 0 && strlen(new_hostname) <= MAX_HOSTNAME_LEN) {
      snprintf(cfg.hostname, sizeof(cfg.hostname), "%s", new_hostname);
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
    snprintf(cfg.syslog_addr, sizeof(cfg.syslog_addr), "%s", new_addr);
    syslog_update_config(new_addr);
    ESP_LOGI(TAG, "Updated syslog_addr to %s", new_addr);
    settings_changed = true;
  }

  cJSON* sntp_server_item = cJSON_GetObjectItem(root, "sntp_server");
  if (cJSON_IsString(sntp_server_item) && sntp_server_item->valuestring) {
    const char* new_server = sntp_server_item->valuestring;
    snprintf(cfg.sntp_server, sizeof(cfg.sntp_server), "%s", new_server);
    ESP_LOGI(TAG, "Updated sntp_server to %s", new_server);
    settings_changed = true;
  }

  cJSON* image_url_item = cJSON_GetObjectItem(root, "image_url");
  if (cJSON_IsString(image_url_item) && image_url_item->valuestring) {
    snprintf(cfg.image_url, sizeof(cfg.image_url), "%s",
             image_url_item->valuestring);
    ESP_LOGI(TAG, "Updated image_url to %s", image_url_item->valuestring);
    settings_changed = true;
  }

  if (settings_changed) {
    config_set(&cfg);
    msg_send_client_info();
  }

  cJSON* reboot_item = cJSON_GetObjectItem(root, "reboot");
  if (cJSON_IsBool(reboot_item) && cJSON_IsTrue(reboot_item)) {
    ESP_LOGI(TAG, "Reboot command received via WS");
    cJSON_Delete(root);
    esp_restart();
  }

  cJSON_Delete(root);
}

void consumer_task(void*) {
  TextMsg msg;
  while (true) {
    if (xQueueReceive(s_text_queue, &msg, portMAX_DELAY) == pdTRUE) {
      process_text_message(msg.data);
      free(msg.data);
    }
  }
}

}  // namespace

void handlers_init() {
  if (s_text_queue) return;

  s_text_queue = xQueueCreate(TEXT_QUEUE_DEPTH, sizeof(TextMsg));
  xTaskCreate(consumer_task, "txt_handler", CONSUMER_STACK_SIZE, nullptr,
              CONSUMER_PRIORITY, &s_consumer_task);
  ESP_LOGI("handlers", "Text message queue initialized");
}

void handlers_deinit() {
  if (s_consumer_task) {
    vTaskDelete(s_consumer_task);
    s_consumer_task = nullptr;
  }

  if (s_text_queue) {
    TextMsg msg;
    while (xQueueReceive(s_text_queue, &msg, 0) == pdTRUE) {
      free(msg.data);
    }
    vQueueDelete(s_text_queue);
    s_text_queue = nullptr;
  }
}

void handle_text_message(esp_websocket_event_data_t* data) {
  bool is_complete =
      (data->payload_offset + data->data_len >= data->payload_len);
  if (!is_complete) return;

  if (!s_text_queue) {
    ESP_LOGW("handlers", "Queue not initialized, dropping text message");
    return;
  }

  auto* buf = static_cast<char*>(malloc(data->data_len + 1));
  if (!buf) {
    ESP_LOGE("handlers", "Failed to allocate text message buffer");
    return;
  }
  memcpy(buf, data->data_ptr, data->data_len);
  buf[data->data_len] = '\0';

  TextMsg msg = {buf, static_cast<size_t>(data->data_len)};
  if (xQueueSend(s_text_queue, &msg, 0) != pdTRUE) {
    ESP_LOGW("handlers", "Text queue full, dropping message");
    free(buf);
  }
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

    if (data->payload_len > CONFIG_HTTP_BUFFER_SIZE_MAX) {
      ESP_LOGE(TAG, "WebP size (%d bytes) exceeds max (%d)", data->payload_len,
               CONFIG_HTTP_BUFFER_SIZE_MAX);
      s_oversize_detected = true;
      if (gfx_display_asset("oversize") != 0) {
        ESP_LOGE(TAG, "Failed to display oversize graphic");
      }
      return;
    }

    if (data->payload_len > 0) {
      s_webp = static_cast<uint8_t*>(heap_caps_malloc(
          static_cast<size_t>(data->payload_len),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!s_webp) {
        ESP_LOGE(TAG, "Failed to allocate WebP buffer (%d bytes)",
                 data->payload_len);
        s_oversize_detected = true;
        return;
      }
    }
  }

  if (s_oversize_detected) return;

  if (data->op_code == 0 && !s_webp) return;

  size_t end_offset = static_cast<size_t>(data->payload_offset) + data->data_len;
  if (end_offset > CONFIG_HTTP_BUFFER_SIZE_MAX) {
    ESP_LOGE(TAG, "WebP size (%zu bytes) exceeds max (%d)", end_offset,
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

  if (data->payload_len > 0 &&
      end_offset > static_cast<size_t>(data->payload_len)) {
    ESP_LOGE(TAG,
             "Invalid WebSocket payload offsets (%zu > total %d); dropping",
             end_offset, data->payload_len);
    free(s_webp);
    s_webp = nullptr;
    s_ws_accumulated_len = 0;
    s_oversize_detected = true;
    return;
  }

  if (data->data_len > 0 && s_webp) {
    memcpy(s_webp + data->payload_offset, data->data_ptr, data->data_len);
  }
  if (end_offset > s_ws_accumulated_len) {
    s_ws_accumulated_len = end_offset;
  }

  bool frame_complete = (data->payload_len > 0)
                            ? (s_ws_accumulated_len >=
                               static_cast<size_t>(data->payload_len))
                            : (data->payload_offset + data->data_len >=
                               data->payload_len);

  if (data->fin && frame_complete) {
    ESP_LOGD(TAG, "WebP download complete (%zu bytes)", s_ws_accumulated_len);

    int counter = gfx_update(s_webp, s_ws_accumulated_len, s_dwell_secs);
    if (counter < 0) {
      ESP_LOGE(TAG, "Failed to queue downloaded WebP");
      free(s_webp);
    }

    if (counter >= 0 && !s_first_image_received) {
      ESP_LOGI(TAG,
               "First WebSocket image received - interrupting boot animation");
      gfx_preempt();
      s_first_image_received = true;
    }

    // Ownership transferred to gfx
    s_webp = nullptr;
    s_ws_accumulated_len = 0;
  }
}
