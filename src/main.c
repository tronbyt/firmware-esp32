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

#define BLUE "\033[1;34m"
#define RESET "\033[0m"  // Reset to default color

static const char* TAG = "main";
int32_t isAnimating =
    5;  // Initialize with a valid value enough time for boot animation
int32_t app_dwell_secs = TIDBYT_REFRESH_INTERVAL_SECONDS;

void app_main(void) {
  ESP_LOGI(TAG, "App Main Start");

  // Setup the device flash storage.
  if (flash_initialize()) {
    ESP_LOGE(TAG, "failed to initialize flash");
    return;
  }
  esp_register_shutdown_handler(&flash_shutdown);

  // Setup the display.
  if (gfx_initialize(ASSET_BOOT_WEBP, ASSET_BOOT_WEBP_LEN)) {
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

  uint8_t mac[6];
  if (!wifi_get_mac(mac)) {
    ESP_LOGI(TAG, "WiFi MAC: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
  }

  ESP_LOGW(TAG, "Main Loop Start");
  for (;;) {
    uint8_t* webp;
    size_t len;
    static int32_t brightness = DISPLAY_DEFAULT_BRIGHTNESS;

    if (remote_get(TIDBYT_REMOTE_URL, &webp, &len, &brightness,
                   &app_dwell_secs)) {
      ESP_LOGE(TAG, "Failed to get webp");
      vTaskDelay(pdMS_TO_TICKS(1 * 1000));
    } else {
      // Successful remote_get
      if (brightness > -1 && brightness < 256) {
        ESP_LOGI(TAG, BLUE "setting brightness to %d" RESET, (int)brightness);
        display_set_brightness(brightness);
      }
      ESP_LOGI(TAG, BLUE "Queuing new webp (%d bytes)" RESET, len);
      gfx_update(webp, len);
      free(webp);
      // Wait for app_dwell_secs to expire (isAnimating will be 0)
      if (isAnimating > 0) ESP_LOGI(TAG, BLUE "Delay for current webp" RESET);
      while (isAnimating > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      ESP_LOGI(TAG, BLUE "Showing new webp" RESET);
      isAnimating = app_dwell_secs;  // use isAnimating as the container for
                                     // app_dwell_secs
    }
  }
}