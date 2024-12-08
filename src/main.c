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
  char new_url[256];

  // Replace "next" with "brightness"
  char* replace = strstr(url, "next");
  if (replace) {
    snprintf(new_url, sizeof(new_url), "%.*sbrightness%s", (int)(replace - url),
             url, replace + strlen("next"));
    ESP_LOGI("URL", "Updated URL: %s", new_url);
  } else {
    ESP_LOGW("URL", "Keyword 'next' not found in URL.");
  }

  

  for (;;) {
    uint8_t* webp;
    size_t len;
    if (remote_get(TIDBYT_REMOTE_URL, &webp, &len)) {
      ESP_LOGE(TAG, "Failed to get webp");
    } else {
      ESP_LOGI(TAG, "Updated webp (%d bytes)", len);
      gfx_update(webp, len);
      free(webp);
    }
    
    #ifdef TIDBYT_REFRESH_INTERVAL_SECONDS
    vTaskDelay(pdMS_TO_TICKS(TIDBYT_REFRESH_INTERVAL_SECONDS * 1000));
    #else
    vTaskDelay(pdMS_TO_TICKS(10000));
    #endif


    
  }
}
