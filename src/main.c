#include <assets.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <webp/demux.h>
#include <esp_wifi.h>

#include "display.h"
#include "flash.h"
#include "gfx.h"
#include "remote.h"
#include "sdkconfig.h"
#include "wifi_manager_wrapper.h"

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
  ESP_LOGI(TAG,"finished flash init");
  esp_register_shutdown_handler(&flash_shutdown);

  // Setup the display.
  if (gfx_initialize(ASSET_BOOT_WEBP, ASSET_BOOT_WEBP_LEN)) {
    ESP_LOGE(TAG, "failed to initialize gfx");
    return;
  }
  esp_register_shutdown_handler(&display_shutdown);

  // Setup WiFi using WiFiManager
  wifi_manager_init();

  // Wait longer for WiFi to connect (10 seconds)
  ESP_LOGI(TAG, "Waiting for WiFi connection...");
  bool wifi_connected = false;
  for (int i = 0; i < 10; i++) {
    if (wifi_manager_is_connected()) {
      ESP_LOGI(TAG, "WiFi connected successfully!");
      wifi_connected = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Waiting for WiFi... %d seconds", i + 1);
  }

  // Only start AP mode if we're not connected to WiFi
  if (!wifi_connected) {
    ESP_LOGW(TAG, "WiFi not connected. Starting AP mode...");
    // Explicitly start the AP mode
    wifi_manager_start_ap();
    ESP_LOGW(TAG, "Please connect to the 'TIDBYT-SETUP' AP to configure WiFi.");
    ESP_LOGW(TAG, "After connecting to the AP, navigate to http://4.3.2.1 in your browser.");

    // Wait for WiFi connection to be established with a timeout
    ESP_LOGW(TAG, "Waiting for WiFi configuration to be completed...");

    // Wait for up to 2 minutes (120 seconds)
    const int max_wait_seconds = 120;
    for (int i = 0; i < max_wait_seconds; i++) {
      if (wifi_manager_is_connected()) {
        ESP_LOGI(TAG, "WiFi configuration completed! Continuing with application.");

        // Wait a bit longer to ensure the client has time to receive the success page
        ESP_LOGI(TAG, "Waiting 5 more seconds for the client to receive the success page...");
        vTaskDelay(pdMS_TO_TICKS(5000));

        break;
      }

      vTaskDelay(pdMS_TO_TICKS(1000));
      ESP_LOGI(TAG, "Waiting for WiFi configuration... %d/%d seconds", i + 1, max_wait_seconds);
    }

    if (!wifi_manager_is_connected()) {
      ESP_LOGW(TAG, "WiFi configuration timed out after %d seconds. Continuing anyway...", max_wait_seconds);
    } else {
      ESP_LOGI(TAG, "WiFi configuration completed! Continuing with application.");
    }
  } else {
    ESP_LOGI(TAG, "Already connected to WiFi, skipping AP mode setup.");
  }

  // If we're connected to WiFi, wait a bit to ensure network services are fully initialized
  if (wifi_manager_is_connected()) {
    ESP_LOGI(TAG, "Connected to WiFi. Waiting 5 seconds for network services to initialize...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // WiFi connection established, continue with application
    ESP_LOGI(TAG, "WiFi connection established. Continuing with application.");
  } else {
    ESP_LOGW(TAG, "WiFi not connected. Some features may not work properly.");
  }

  uint8_t mac[6];
  wifi_manager_get_mac(mac);
  ESP_LOGI(TAG, "WiFi MAC: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);

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