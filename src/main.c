#include <assets.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <webp/demux.h>

#include "display.h"
#include "flash.h"
#include "gfx.h"
#include "remote.h"
#include "sdkconfig.h"
#include "wifi.h"

static const char* TAG = "main";
int32_t isAnimating = 0;  // Initialize with a valid value
char brightness_url[256];

// void update_brightness() {
//   // remote_get the brightness_url
//   static size_t len;
//   size_t b = DISPLAY_DEFAULT_BRIGHTNESS;
//   if (remote_get(brightness_url, (uint8_t**)&b , &len, &b)) {
//     ESP_LOGE(TAG, "Failed to get brightness");
//   } else {
//     ESP_LOGI(TAG, "Got Brightness (%d)", b);
//     if ( b < 100 ) {
//       display_set_brightness(b);
//     }
//   }
// }

void app_main(void) {
  ESP_LOGI(TAG, "Hello world!");

  // Setup the device flash storage.
  if (flash_initialize()) {
    ESP_LOGE(TAG, "failed to initialize flash");
    return;
  }
  esp_register_shutdown_handler(&flash_shutdown);

  // Setup the display.
  if (gfx_initialize(ASSET_NOAPPS_WEBP, ASSET_NOAPPS_WEBP_LEN)) {
    ESP_LOGE(TAG, "failed to initialize gfx");
    return;
  }
  esp_register_shutdown_handler(&display_shutdown);

  // Setup WiFi.
  if (wifi_initialize(TIDBYT_WIFI_SSID, TIDBYT_WIFI_PASSWORD)) {
    ESP_LOGE(TAG, "failed to initialize WiFi");
    return;
  }
  esp_register_shutdown_handler(&wifi_shutdown);

  char url[256] = TIDBYT_REMOTE_URL;

  // Replace "next" with "brightness"
  char* replace = strstr(url, "next");
  if (replace) {
    snprintf(brightness_url, sizeof(brightness_url), "%.*sbrightness%s",
             (int)(replace - url), url, replace + strlen("next"));
    ESP_LOGI("URL", "Updated: %s", brightness_url);
  } else {
    ESP_LOGW("URL", "Keyword 'next' not found in URL.");
  }

  // update_brightness();
  int64_t start_time = esp_timer_get_time();

  for (;;) {
    // ESP_LOGW(TAG,"Main Loop Start");
    static int count = 0;
    static int animationCount = 0;
    uint8_t* webp;
    size_t len;
    static int brightness = DISPLAY_DEFAULT_BRIGHTNESS;
    static int app_dwell_secs = TIDBYT_REFRESH_INTERVAL_SECONDS;

    if (remote_get(TIDBYT_REMOTE_URL, &webp, &len, &brightness, &app_dwell_secs)) {
      ESP_LOGE(TAG, "Failed to get webp");
      vTaskDelay(pdMS_TO_TICKS(1 * 1000));

    } else {
      // Successful remote_get
      ESP_LOGI(TAG, "Queued webp (%d bytes)", len);
      gfx_update(webp, len);
      free(webp);

      // If the previous app is still animating or still within app_dwell then wait until it's done before updating to the next app
      // if (isAnimating == 1 or ) ESP_LOGW(TAG,"delay for animation");
      unsigned int app_dwell_ms = app_dwell_secs * 1000;
      while (esp_timer_get_time() - start_time < app_dwell_ms || isAnimating == 1) {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      isAnimating = -1; // signal gfx_loop to get out and load new webp
      // ESP_LOGW(TAG,"done delay for animation");
      if (brightness > -1 && brightness < 256) {
        // ESP_LOGI(TAG, "Set brightness to %i", brightness);
        display_set_brightness(brightness);
        // ESP_LOGI(TAG, "Delaying (%d secs)", app_dwell_secs);
        // vTaskDelay(pdMS_TO_TICKS(app_dwell_secs * 1000));
      }
      while (isAnimating != 1) {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      start_time = esp_timer_get_time();
    }

  }

}