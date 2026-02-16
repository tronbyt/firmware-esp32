#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ap.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_settings.h"
#include "sntp.h"

#define TAG "WIFI"

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_CONNECTED_IPV6_BIT BIT2

// Maximum number of reconnection attempts before giving up
#define MAX_RECONNECT_ATTEMPTS 10

// Static variables
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t* s_sta_netif = NULL;
static void (*s_config_callback)(void) = NULL;

// Reconnection counter
static int s_reconnect_attempts = 0;
static bool s_connection_given_up = false;

// Counter for tracking consecutive WiFi disconnections
static int s_wifi_disconnect_counter = 0;

// Function prototypes
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data);

// Initialize WiFi
int wifi_initialize(const char* ssid, const char* password) {
  ESP_LOGI(TAG, "Initializing WiFi");

  if (!nvs_get_ap_mode()) {
    ESP_LOGI(TAG, "AP mode disabled via settings");
  }

  // Create event group
  s_wifi_event_group = xEventGroupCreate();

  // Initialize TCP/IP adapter
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Configure SNTP before WiFi/DHCP starts to ensure DHCP NTP options are
  // captured
  app_sntp_config();

  // Create default STA and AP network interfaces
  s_sta_netif = esp_netif_create_default_wifi_sta();
  if (nvs_get_ap_mode()) {
    ap_init_netif();
  }

  // Initialize WiFi with default config
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Configure Hostname
  char hostname[MAX_HOSTNAME_LEN + 1];
  nvs_get_hostname(hostname, sizeof(hostname));
  if (strlen(hostname) == 0) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(hostname, sizeof(hostname), "tronbyt-%02x%02x%02x", mac[3], mac[4],
             mac[5]);
    ESP_LOGI(TAG, "Generated default hostname: %s", hostname);
    nvs_set_hostname(hostname);
    nvs_save_settings();
  }
  wifi_set_hostname(hostname);

  // Register event handlers
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6,
                                             &wifi_event_handler, NULL));

  char saved_ssid[33] = {0};
  nvs_get_ssid(saved_ssid, sizeof(saved_ssid));
  bool has_credentials = (strlen(saved_ssid) > 0);

  if (nvs_get_ap_mode()) {
    ap_configure();
  } else {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  }

  // Configure STA with credentials if available
  if (has_credentials) {
    wifi_config_t sta_config = {0};
    nvs_get_ssid((char*)sta_config.sta.ssid, sizeof(sta_config.sta.ssid));
    nvs_get_password((char*)sta_config.sta.password,
                     sizeof(sta_config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_LOGI(TAG, "Configured STA with SSID: %s", saved_ssid);
  }

  // Start WiFi
  ESP_ERROR_CHECK(esp_wifi_start());

  /* Apply WiFi Power Save Mode from configuration */
  wifi_apply_power_save();

  int8_t tx_power;
  ESP_ERROR_CHECK(esp_wifi_get_max_tx_power(&tx_power));
  ESP_LOGI(TAG, "Max TX Power (Current): %.2f dBm", tx_power * 0.25f);

#ifdef CONFIG_IDF_TARGET_ESP32S3
  // Set max TX power to 11dBm (44 units) for S3
  ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(44));
  ESP_ERROR_CHECK(esp_wifi_get_max_tx_power(&tx_power));
  ESP_LOGI(TAG, "Max TX Power (S3 limit applied): %.2f dBm", tx_power * 0.25f);
#endif

  // Only attempt to connect if we have valid saved credentials
  if (!has_credentials) {
    if (nvs_get_ap_mode()) {
      ESP_LOGI(TAG,
               "No valid WiFi credentials available, starting in AP mode only");
    } else {
      ESP_LOGW(TAG,
               "No valid WiFi credentials available and AP mode is disabled");
    }
    // Reset any previous connection attempts
    s_reconnect_attempts = MAX_RECONNECT_ATTEMPTS;
    s_connection_given_up = true;
  }

  ESP_LOGI(TAG, "WiFi initialized successfully");
  return 0;
}

// Shutdown WiFi
void wifi_shutdown(void) {
  // Stop the web server if it's running
  ap_stop();

  // Stop WiFi
  esp_wifi_stop();
  esp_wifi_deinit();

  // Unregister event handlers
  esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               &wifi_event_handler);
  esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifi_event_handler);
  esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &wifi_event_handler);

  // Delete event group
  if (s_wifi_event_group != NULL) {
    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = NULL;
  }
}

// Get MAC address
int wifi_get_mac(uint8_t mac[6]) {
  esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get MAC address: %s", esp_err_to_name(err));
    return 1;
  }
  return 0;
}

// Set Hostname
int wifi_set_hostname(const char* hostname) {
  if (s_sta_netif) {
    esp_err_t err = esp_netif_set_hostname(s_sta_netif, hostname);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Hostname set to: %s", hostname);
      return 0;
    } else {
      ESP_LOGE(TAG, "Failed to set hostname: %s", esp_err_to_name(err));
      return 1;
    }
  }
  return 1;
}

// Check if WiFi is connected
bool wifi_is_connected(void) {
  return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

// Wait for WiFi connection with timeout
bool wifi_wait_for_connection(uint32_t timeout_ms) {
  ESP_LOGI(TAG, "Waiting for WiFi connection (timeout: %lu ms)",
           (unsigned long)timeout_ms);

  // If already connected, return immediately
  if (wifi_is_connected()) {
    ESP_LOGI(TAG, "Already connected to WiFi");
    return true;
  }

  char saved_ssid[33] = {0};
  nvs_get_ssid(saved_ssid, sizeof(saved_ssid));
  if (strlen(saved_ssid) == 0) {
    ESP_LOGI(TAG, "No saved config, won't connect.");
    return false;
  }

  // Wait for connection or timeout
  TickType_t start_ticks = xTaskGetTickCount();
  TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

  while (xTaskGetTickCount() - start_ticks < timeout_ticks) {
    if (wifi_is_connected()) {
      ESP_LOGI(TAG, "Connected to WiFi");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(100));  // Check every 100ms
  }

  ESP_LOGW(TAG, "WiFi connection timeout");
  return false;
}

// Wait for IPv6 address with timeout
bool wifi_wait_for_ipv6(uint32_t timeout_ms) {
  if (s_wifi_event_group == NULL) return false;

  if (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_IPV6_BIT) {
    return true;
  }

  ESP_LOGI(TAG, "Waiting for IPv6 address (timeout: %lu ms)",
           (unsigned long)timeout_ms);
  EventBits_t bits =
      xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_IPV6_BIT, pdFALSE,
                          pdTRUE, pdMS_TO_TICKS(timeout_ms));

  if (bits & WIFI_CONNECTED_IPV6_BIT) {
    return true;
  }

  ESP_LOGI(TAG, "IPv6 address wait timeout");
  return false;
}

// Add new function to register config callback
void wifi_register_config_callback(void (*callback)(void)) {
  s_config_callback = callback;
}

// Helper to handle successful IP acquisition
static void handle_successful_ip_acquisition(void) {
  // Reset reconnection counter on successful connection
  s_reconnect_attempts = 0;
  s_connection_given_up = false;

  // Set connection bit and clear fail bit
  xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
  xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START:
        // STA started, reset reconnection counter and try to connect
        s_reconnect_attempts = 0;
        s_connection_given_up = false;
        esp_wifi_connect();
        break;
      case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Connected to AP, creating IPv6 link local address");
        esp_netif_create_ip6_linklocal(s_sta_netif);
        break;
      case WIFI_EVENT_STA_DISCONNECTED:
        // Increment reconnection counter
        s_reconnect_attempts++;

        // Clear connection bit and set fail bit
        xEventGroupClearBits(s_wifi_event_group,
                             WIFI_CONNECTED_BIT | WIFI_CONNECTED_IPV6_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);

        // Check if we've reached the maximum number of reconnection attempts
        if (nvs_get_ap_mode() &&
            s_reconnect_attempts >= MAX_RECONNECT_ATTEMPTS &&
            !s_connection_given_up) {
          ESP_LOGW(TAG, "Maximum reconnection attempts (%d) reached, giving up",
                   MAX_RECONNECT_ATTEMPTS);
          s_connection_given_up = true;
          // We'll continue in AP mode only at this point
        } else if (!s_connection_given_up) {
          // Only try to reconnect if we haven't given up yet
          ESP_LOGI(TAG,
                   "WiFi disconnected, trying to reconnect... (attempt %d)",
                   s_reconnect_attempts);
          esp_wifi_connect();
        }
        break;
      case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t* event =
            (wifi_event_ap_staconnected_t*)event_data;
        ESP_LOGI(TAG, "Station joined, AID=%d", event->aid);
      } break;
      case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t* event =
            (wifi_event_ap_stadisconnected_t*)event_data;
        ESP_LOGI(TAG, "Station left, AID=%d", event->aid);
      } break;
      default:
        break;
    }
  } else if (event_base == IP_EVENT) {
    switch (event_id) {
      case IP_EVENT_STA_GOT_IP: {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        handle_successful_ip_acquisition();
      } break;
      case IP_EVENT_GOT_IP6: {
        ip_event_got_ip6_t* event = (ip_event_got_ip6_t*)event_data;
        ip6_addr_t* addr = (ip6_addr_t*)&event->ip6_info.ip;
        ESP_LOGI(TAG, "Got IPv6 address: " IPV6STR,
                 IPV62STR(event->ip6_info.ip));

        if (ip6_addr_isglobal(addr)) {
          ESP_LOGI(TAG, "IPv6 address acquired");
          xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_IPV6_BIT);
          handle_successful_ip_acquisition();
        } else {
          ESP_LOGI(TAG, "IPv6 address is not global, waiting...");
        }
      } break;
      default:
        break;
    }
  }
}

// Helper to handle successful IP acquisition

/**
 * @brief Check WiFi health and attempt reconnection if needed
 *
 * This function checks if WiFi is connected. If not, it attempts to reconnect
 * and increments a counter. If the counter reaches 10 consecutive failures,
 * the system will reboot. The counter is reset whenever WiFi is connected.
 */
void wifi_health_check(void) {
  if (wifi_is_connected()) {
    // Reset counter when WiFi is connected
    if (s_wifi_disconnect_counter > 0) {
      // ESP_LOGI(TAG, "WiFi reconnected successfully, resetting disconnect
      // counter");
      s_wifi_disconnect_counter = 0;
    }
    return;
  }

  // WiFi is not connected, increment counter
  s_wifi_disconnect_counter++;
  ESP_LOGW(TAG, "WiFi Health check. Disconnect count: %d",
           s_wifi_disconnect_counter);

  if (s_wifi_disconnect_counter >= 10) {
    ESP_LOGE(TAG, "WiFi disconnect count reached %d - rebooting",
             s_wifi_disconnect_counter);
    esp_restart();
  }

  // Try to reconnect
  char saved_ssid[33] = {0};
  nvs_get_ssid(saved_ssid, sizeof(saved_ssid));
  if (strlen(saved_ssid) > 0) {
    ESP_LOGI(TAG, "Reconnecting in Health check...");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "WiFi reconnect attempt failed: %s", esp_err_to_name(err));
    }
  } else {
    ESP_LOGW(TAG, "No SSID configured, cannot reconnect");
  }
}

void wifi_apply_power_save(void) {
  int power_save_mode = nvs_get_wifi_power_save();
  ESP_LOGI(TAG, "Setting WiFi Power Save Mode to %d...", power_save_mode);
  esp_wifi_set_ps((wifi_ps_type_t)power_save_mode);
}
