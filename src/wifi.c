#include <esp_log.h>
#include <esp_wifi.h>
#include <string.h>
#include "simple_wifi_manager.h"

static const char* TAG = "wifi";

int wifi_initialize(const char* ssid, const char* password) {
  // Initialize the simple WiFi manager
  esp_err_t err = simple_wifi_manager_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize WiFi manager: %s", esp_err_to_name(err));
    return 1;
  }

  // Start the WiFi manager
  err = simple_wifi_manager_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start WiFi manager: %s", esp_err_to_name(err));
    return 1;
  }

  ESP_LOGI(TAG, "WiFi manager started successfully");

  // Note: We don't wait for connection here, as the WiFi manager will handle
  // connection in the background. The user will configure WiFi through the web interface.

  return 0;
}

void wifi_shutdown() {
  // The simple WiFi manager doesn't have a shutdown function,
  // but we can stop WiFi directly
  esp_wifi_stop();
  esp_wifi_deinit();
}

int wifi_get_mac(uint8_t mac[6]) {
  esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get MAC address: %s", esp_err_to_name(err));
    return 1;
  }
  return 0;
}

bool wifi_wait_for_connection(uint32_t timeout_ms) {
  return simple_wifi_manager_wait_for_connection(timeout_ms);
}

const char* wifi_get_image_url() {
  return simple_wifi_manager_get_image_url();
}
