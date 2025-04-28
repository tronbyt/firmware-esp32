#include <assets.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <webp/demux.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_system.h>
#include <esp_http_server.h>
#include <nvs_flash.h>
#include <esp_wifi_types.h>
#include <esp_err.h>
#include <esp_netif.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip6_addr.h>
#include <lwip/inet.h>

#include "display.h"
#include "flash.h"
#include "gfx.h"
#include "remote.h"
#include "sdkconfig.h"
#include "WifiManagerEsp32.hpp"
#include "nvs_flash.h"

#define BLUE "\033[1;34m"
#define RESET "\033[0m"  // Reset to default color

// Define constants that were previously defined elsewhere
#define TIDBYT_REFRESH_INTERVAL_SECONDS 60
#define TIDBYT_REMOTE_URL "http://192.168.1.236:8000/8207bf0d/next"
#define DISPLAY_DEFAULT_BRIGHTNESS 100

static const char* TAG = "main";
int32_t isAnimating = 5;  // Initialize with a valid value enough time for boot animation
int32_t app_dwell_secs = TIDBYT_REFRESH_INTERVAL_SECONDS;

// Global WiFi manager instance
WifiManagerEsp32* wifiManager = nullptr;

// Function to get MAC address
void get_mac(uint8_t* mac) {
  esp_wifi_get_mac(WIFI_IF_STA, mac);
}

// Function to check if WiFi is connected
// Event handler for WiFi events
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data) {
  if (event_base == WIFI_EVENT) {
    if (event_id == WIFI_EVENT_STA_START) {
      ESP_LOGI(TAG, "WiFi station started, connecting...");
      esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
      ESP_LOGI(TAG, "WiFi connected!");
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
      ESP_LOGI(TAG, "WiFi disconnected, trying to reconnect...");
      esp_wifi_connect();
    }
  } else if (event_base == IP_EVENT) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
      ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
      ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
    }
  }
}

// Connect to WiFi using standard ESP-IDF method
bool connect_wifi_standard(const char* ssid, const char* password) {
  ESP_LOGI(TAG, "Connecting to WiFi using standard ESP-IDF method");

  // Initialize TCP/IP adapter
  ESP_ERROR_CHECK(esp_netif_init());

  // Create default event loop if it doesn't exist
  esp_err_t err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(err);
  }

  // Create default WiFi station
  esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);

  // Initialize WiFi with default config
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Register event handlers
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

  // Configure WiFi station
  wifi_config_t wifi_config = {0};
  strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
  strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

  // Set WiFi mode to station
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  // Start WiFi
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "WiFi initialization completed");

  // Wait for connection
  int retry_count = 30;
  while (retry_count > 0) {
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Connected to WiFi SSID: %s", ap_info.ssid);
      return true;
    }
    ESP_LOGI(TAG, "Waiting for WiFi connection... %d", retry_count);
    vTaskDelay(pdMS_TO_TICKS(1000));
    retry_count--;
  }

  ESP_LOGW(TAG, "Failed to connect to WiFi");
  return false;
}

bool is_wifi_connected() {
  // Check if the WiFi is connected directly using ESP-IDF API
  wifi_ap_record_t ap_info;
  esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Connected to WiFi SSID: %s", ap_info.ssid);
    return true;
  }

  // If we're using WiFiManager, also check through that
  if (wifiManager != nullptr && wifiManager->credentials_opt.has_value()) {
    // Additional check through WiFiManager if needed
    // This is a fallback and shouldn't be necessary if the direct check works
  }

  return false;
}

extern "C" void app_main(void) {
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

  // Initialize NVS flash (required for WiFiManager)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Register event handlers
  // Create the event loop if it doesn't exist
  esp_err_t err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(err);
  }

  // Try to connect using standard ESP-IDF method first with credentials from secrets.json
  ESP_LOGI(TAG, "Attempting to connect to WiFi using standard ESP-IDF method");
  bool connected = connect_wifi_standard("BeeBee2.4", "bravestreet599");

  if (!connected) {
    ESP_LOGI(TAG, "Standard WiFi connection failed, falling back to WiFiManager");

    // Initialize WiFiManager with custom config
    WifiManagerIdfConfig config;
    // Set a more visible SSID
    config.ssid = "TIDBYT-SETUP";
    // Don't keep AP mode on after connecting to WiFi
    config.shouldKeepAP = false;
    // Enable debug logging
    config.enableLogger = true;

    // Create the WiFiManager instance
    wifiManager = new WifiManagerEsp32(config);

    // Print debug info
    ESP_LOGI(TAG, "Starting WiFi setup...");

    // Explicitly start the AP mode and server
    wifiManager->setupWiFi(true, true);
    wifiManager->setupServerAndDns();

    ESP_LOGI(TAG, "WiFi setup complete!");

    // Print WiFi status
    if (wifiManager->credentials_opt.has_value()) {
      ESP_LOGI(TAG, "WiFi credentials found: SSID=%s", wifiManager->credentials_opt.value()["ssid"].c_str());
    } else {
      ESP_LOGI(TAG, "No WiFi credentials found");
    }
  } else {
    ESP_LOGI(TAG, "Successfully connected to WiFi using standard ESP-IDF method");
  }

  // Wait for WiFi connection
  ESP_LOGI(TAG, "Waiting for WiFi connection...");
  int timeout = 30; // 30 seconds timeout
  while (!is_wifi_connected() && timeout > 0) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    timeout--;
    ESP_LOGI(TAG, "Waiting for WiFi... %d seconds left", timeout);
  }

  if (!is_wifi_connected()) {
    ESP_LOGW(TAG, "WiFi connection timeout. Continuing without WiFi.");
  } else {
    ESP_LOGI(TAG, "WiFi connected successfully!");

    // If we're connected to WiFi, wait longer to ensure network services are fully initialized
    ESP_LOGI(TAG, "Connected to WiFi. Waiting 15 seconds for network services to initialize...");
    vTaskDelay(pdMS_TO_TICKS(15000));

    // Try to ping the server to ensure network connectivity
    ESP_LOGI(TAG, "Testing network connectivity...");
  }

  uint8_t mac[6];
  get_mac(mac);
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
