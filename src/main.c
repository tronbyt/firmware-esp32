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
#include "wifi_manager.h"


#define BLUE "\033[1;34m"
#define RESET "\033[0m"  // Reset to default color

static const char* TAG = "main";
int32_t isAnimating =
    5;  // Initialize with a valid value enough time for boot animation
int32_t app_dwell_secs = TIDBYT_REFRESH_INTERVAL_SECONDS;
bool is_connected = false;

void cb_connection_ok(void* pvParameter) {
  ESP_LOGI(TAG, "WiFi have a connection!");
  is_connected = true;
}

void app_main(void) {
  ESP_LOGI(TAG, "App Main Start");

  // Setup the device flash storage.
  if (flash_initialize()) {
    ESP_LOGE(TAG, "failed to initialize flash");
    return;
  }
  ESP_LOGI(TAG,"finished flash init");
  esp_register_shutdown_handler(&flash_shutdown);

  // Setup the display.
  if (gfx_initialize(ASSET_BOOT_WEBP, ASSET_BOOT_WEBP_LEN)) {
    ESP_LOGE(TAG, "failed to initialize gfx");
    return;
  }
  esp_register_shutdown_handler(&display_shutdown);

  esp_register_shutdown_handler(&wifi_shutdown);
  wifi_manager_init();
  wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);
  wifi_manager_start();

  uint8_t mac[6];
  if (!wifi_get_mac(mac)) {
    ESP_LOGI(TAG, "WiFi MAC: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
  }

  // Wait for WiFi connection
  ESP_LOGI(TAG, "Waiting for WiFi connection...");
  while (!is_connected) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  ESP_LOGW(TAG, "Main Loop Start");
  for (;;) {
    uint8_t* webp;
    size_t len;
    static uint8_t brightness_pct = DISPLAY_DEFAULT_BRIGHTNESS;

    if (remote_get(TIDBYT_REMOTE_URL, &webp, &len, &brightness_pct,
                   &app_dwell_secs)) {
      ESP_LOGE(TAG, "Failed to get webp");
      vTaskDelay(pdMS_TO_TICKS(1 * 1000));
    } else {
      // Successful remote_get
      display_set_brightness(brightness_pct);
      ESP_LOGI(TAG, BLUE "Queuing new webp (%d bytes)" RESET, len);
      gfx_update(webp, len);
      free(webp);
      // Wait for app_dwell_secs to expire (isAnimating will be 0)
      ESP_LOGI(TAG, BLUE "isAnimating is %d" RESET, (int)isAnimating);
      if (isAnimating > 0) ESP_LOGI(TAG, BLUE "Delay for current webp" RESET);
      while (isAnimating > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      ESP_LOGI(TAG, BLUE "Setting isAnimating to %d" RESET, (int)app_dwell_secs);
      isAnimating = app_dwell_secs;  // use isAnimating as the container for
                                     // app_dwell_secs
      vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
  }
}
