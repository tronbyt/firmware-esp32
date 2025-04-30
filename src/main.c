#include <assets.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <string.h>
#include <webp/demux.h>

#include "config.h"
#include "display.h"
#include "flash.h"
#include "gfx.h"
#include "remote.h"
#include "sdkconfig.h"
#include "wifi.h"
#include "wifi_manager.h"

// External declaration for wifi_settings from wifi_manager.c
extern wifi_settings_t wifi_settings;


#define BLUE "\033[1;34m"
#define RESET "\033[0m"  // Reset to default color

static const char* TAG = "main";
int32_t isAnimating =
    5;  // Initialize with a valid value enough time for boot animation
int32_t app_dwell_secs = TIDBYT_REFRESH_INTERVAL_SECONDS;
bool is_connected = false;
bool using_wifi_manager = false;
int connection_timeout = 0; // Will be set during connection attempt

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

  // Set custom AP SSID and password before initializing WiFi Manager
  strcpy((char*)wifi_settings.ap_ssid, "Tronbyt-Config");
  strcpy((char*)wifi_settings.ap_pwd, ""); // Empty password for open AP

  // Initialize and start WiFi Manager
  wifi_manager_init();
  // Give the WiFi manager time to initialize
  vTaskDelay(pdMS_TO_TICKS(500));
  wifi_manager_start();
  // Give the WiFi manager time to start
  vTaskDelay(pdMS_TO_TICKS(500));
  wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);

  ESP_LOGI(TAG, "WiFi Manager initialized with AP SSID: %s (no password)", (char*)wifi_settings.ap_ssid);

  // First try to connect with hardcoded WiFi credentials
  ESP_LOGI(TAG, "Attempting to connect with hardcoded WiFi credentials...");

  // Use WiFi Manager to connect with the hardcoded credentials
  wifi_manager_connect_async(true, TIDBYT_WIFI_SSID, TIDBYT_WIFI_PASSWORD);

  // Wait for WiFi connection
  ESP_LOGI(TAG, "Waiting for WiFi connection...");
  connection_timeout = 150; // 15 seconds (150 * 100ms)
  while (!is_connected && connection_timeout > 0) {
    vTaskDelay(pdMS_TO_TICKS(100));
    connection_timeout--;
  }

  // Give the WiFi manager time to update the status
  vTaskDelay(pdMS_TO_TICKS(500));

  if (is_connected) {
    // Successfully connected with hardcoded credentials
    ESP_LOGI(TAG, "Successfully connected with hardcoded credentials");
    using_wifi_manager = true; // Still using WiFi Manager, but with hardcoded credentials
  } else {
    // Failed to connect with hardcoded credentials, WiFi Manager will start AP mode automatically
    ESP_LOGI(TAG, "Failed to connect with hardcoded credentials, WiFi Manager will start AP mode");
    using_wifi_manager = true;

    // Continue waiting for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection via AP mode...");
    while (!is_connected) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }


  uint8_t mac[6];
  if (!wifi_get_mac(mac)) {
    ESP_LOGI(TAG, "WiFi MAC: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
  }

  // Get and display the IP address
  if (using_wifi_manager && wifi_manager_is_sta_connected()) {
    // If using WiFi Manager, get the IP address from it
    char *ip_addr = wifi_manager_get_sta_ip_string();
    ESP_LOGI(TAG, "Connected with IP address: %s", ip_addr);
  } else if (is_connected) {
    // If using hardcoded credentials, we don't have a way to get the IP address string
    // but we know we're connected
    ESP_LOGI(TAG, "Connected with hardcoded credentials");
  }

  ESP_LOGW(TAG, "Main Loop Start");
  for (;;) {
    uint8_t* webp;
    size_t len;
    static uint8_t brightness_pct = DISPLAY_DEFAULT_BRIGHTNESS;

    // Get the image URL
    char *image_url = NULL;
    const char *url_to_use = NULL;

    // Check if we're connected to WiFi
    if (is_connected) {
      // If we connected with hardcoded credentials within the timeout period
      if (connection_timeout > 0) {
        // Use the hardcoded image URL
        url_to_use = TIDBYT_REMOTE_URL;
        ESP_LOGI(TAG, "Connected with hardcoded credentials, using hardcoded image URL");
      } else {
        // We connected via WiFi Manager's AP mode
        image_url = wifi_manager_get_image_url();
        if (image_url != NULL) {
          // Use the image URL saved during WiFi setup
          url_to_use = image_url;
          ESP_LOGI(TAG, "Connected via WiFi Manager, using saved image URL");
        } else {
          // No saved image URL, use the default
          url_to_use = TIDBYT_REMOTE_URL;
          ESP_LOGI(TAG, "Connected via WiFi Manager, no saved image URL, using default");
        }
      }
    } else {
      // Not connected, use the hardcoded image URL
      url_to_use = TIDBYT_REMOTE_URL;
      ESP_LOGI(TAG, "Not connected to WiFi, using default image URL");
    }

    ESP_LOGI(TAG, "Using image URL: %s", url_to_use);

    if (remote_get(url_to_use, &webp, &len, &brightness_pct,
                   &app_dwell_secs)) {
      ESP_LOGE(TAG, "Failed to get webp");
      vTaskDelay(pdMS_TO_TICKS(1 * 1000));
    } else {
      // Free the image URL if it was allocated
      if (image_url != NULL) {
        free(image_url);
      }
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
