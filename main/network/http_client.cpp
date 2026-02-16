#include "http_client.h"

#include <cstdlib>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "display.h"
#include "nvs_settings.h"
#include "ota.h"
#include "remote.h"
#include "sdkconfig.h"
#include "webp_player.h"
#include "wifi.h"

namespace {

const char* TAG = "http_client";

#ifndef CONFIG_REFRESH_INTERVAL_SECONDS
constexpr int DEFAULT_REFRESH_INTERVAL = 10;
#else
constexpr int DEFAULT_REFRESH_INTERVAL = CONFIG_REFRESH_INTERVAL_SECONDS;
#endif

int32_t s_dwell_secs = DEFAULT_REFRESH_INTERVAL;

void ota_task_entry(void* param) {
  auto* url = static_cast<char*>(param);
  run_ota(url);
  free(url);
  vTaskDelete(nullptr);
}

}  // namespace

void http_client_run_loop(const char* url) {
  ESP_LOGW(TAG, "HTTP Loop Start with URL: %s", url);

  static uint8_t brightness_pct = (CONFIG_HUB75_BRIGHTNESS * 100) / 255;

  while (true) {
    uint8_t* webp;
    size_t len;
    int status_code = 0;
    char* ota_url = nullptr;

    ESP_LOGI(TAG, "Fetching from URL: %s", url);

    int64_t fetch_start_us = esp_timer_get_time();
    bool fetch_failed =
        !wifi_is_connected() ||
        remote_get(url, &webp, &len, &brightness_pct, &s_dwell_secs,
                   &status_code, &ota_url);
    int64_t fetch_duration_ms =
        (esp_timer_get_time() - fetch_start_us) / 1000;

    ESP_LOGI(TAG, "HTTP fetch returned in %lld ms", fetch_duration_ms);

    if (ota_url) {
      ESP_LOGI(TAG, "OTA URL received via HTTP: %s", ota_url);
      xTaskCreate(ota_task_entry, "ota_task", 8192, ota_url, 5, nullptr);
    }

    if (fetch_failed) {
      ESP_LOGE(TAG, "No WiFi or Failed to get webp with code %d", status_code);
      vTaskDelay(pdMS_TO_TICKS(1000));
      draw_error_indicator_pixel();
      if (status_code == 0) {
        ESP_LOGI(TAG, "No connection");
      } else if (status_code == 404 || status_code == 400) {
        ESP_LOGI(TAG, "HTTP 404/400, displaying 404");
        if (gfx_display_asset("error_404")) {
          ESP_LOGE(TAG, "Failed to display 404 screen");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
      } else if (status_code == 413) {
        ESP_LOGI(TAG,
                 "Content too large - oversize graphic already displayed");
        vTaskDelay(pdMS_TO_TICKS(5000));
      }
    } else {
      display_set_brightness(brightness_pct);
      ESP_LOGI(TAG, "Queuing new webp (%zu bytes)", len);

      int queued_counter = gfx_update(webp, len, s_dwell_secs);
      // Ownership transferred to gfx
      webp = nullptr;

      if (gfx_is_animating()) {
        ESP_LOGI(TAG, "Waiting for current webp to finish");
        gfx_wait_idle();
      }

      int timeout = 0;
      while (gfx_get_loaded_counter() != queued_counter && timeout < 20000) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout += 10;
      }
      if (timeout >= 20000) {
        ESP_LOGE(TAG, "Timeout waiting for gfx task to load image");
      } else {
        ESP_LOGI(TAG, "Gfx task loaded image counter %d", queued_counter);
      }
    }
    wifi_health_check();
  }
}
