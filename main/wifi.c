#include "wifi.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip4_addr.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "esp_wifi_types.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs_settings.h"

#define TAG "WIFI"

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_CONNECTED_IPV6_BIT BIT2

// Maximum number of reconnection attempts before giving up
#define MAX_RECONNECT_ATTEMPTS 10

// Static variables
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_sta_netif = NULL;
static void (*s_config_callback)(void) = NULL;

// Reconnection counter
static int s_reconnect_attempts = 0;
static bool s_connection_given_up = false;

// Counter for tracking consecutive WiFi disconnections
static int s_wifi_disconnect_counter = 0;

#include "ap.h"

// Callback functions
static void (*s_connect_callback)(void) = NULL;
static void (*s_disconnect_callback)(void) = NULL;

// Function prototypes
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

// Initialize WiFi
int wifi_initialize(const char *ssid, const char *password) {
  ESP_LOGI(TAG, "Initializing WiFi");

  // Initialize NVS settings
  ESP_ERROR_CHECK(nvs_settings_init());

  if (!nvs_get_ap_mode()) {
      ESP_LOGI(TAG, "AP mode disabled via settings");
  }

  // Create event group
  s_wifi_event_group = xEventGroupCreate();

  // Initialize TCP/IP adapter
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Create default STA and AP network interfaces
  s_sta_netif = esp_netif_create_default_wifi_sta();
#if ENABLE_AP_MODE
  if (nvs_get_ap_mode()) {
      ap_init_netif();
  }
#endif

  // Initialize WiFi with default config
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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

#if ENABLE_AP_MODE
  if (nvs_get_ap_mode()) {
      ap_configure();
  } else {
      ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  }
#else
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
#endif

  // Configure STA with credentials if available
  if (has_credentials) {
      wifi_config_t sta_config = {0};
      nvs_get_ssid((char *)sta_config.sta.ssid, sizeof(sta_config.sta.ssid));
      nvs_get_password((char *)sta_config.sta.password, sizeof(sta_config.sta.password));
      ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
      ESP_LOGI(TAG, "Configured STA with SSID: %s", saved_ssid);
  }

  // Start WiFi
  ESP_ERROR_CHECK(esp_wifi_start());

  /* Apply WiFi Power Save Mode from configuration */
  wifi_apply_power_save();

  // Wait for AP to start
#if ENABLE_AP_MODE
  if (nvs_get_ap_mode()) {
      vTaskDelay(pdMS_TO_TICKS(500));
      // Start the web server
      ap_start();
  }
#endif

  // Only attempt to connect if we have valid saved credentials
  if (!has_credentials) {
#if ENABLE_AP_MODE
    if (nvs_get_ap_mode()) {
        ESP_LOGI(TAG, "No valid WiFi credentials available, starting in AP mode only");
    } else {
        ESP_LOGW(TAG, "No valid WiFi credentials available and AP mode is disabled");
    }
#else
    ESP_LOGW(TAG,
             "No valid WiFi credentials available and AP mode is disabled");
#endif
    // Reset any previous connection attempts
    s_reconnect_attempts = MAX_RECONNECT_ATTEMPTS;
    s_connection_given_up = true;
  }

  ESP_LOGI(TAG, "WiFi initialized successfully");
  return 0;
}

#if ENABLE_AP_MODE
void wifi_shutdown_ap(TimerHandle_t xTimer) {
    ap_shutdown_timer_callback(xTimer);
}
#endif

// Shutdown WiFi
void wifi_shutdown(void) {
#if ENABLE_AP_MODE
    // Stop the web server if it's running
    ap_stop();
#endif

    // Stop WiFi
    esp_wifi_stop();
    esp_wifi_deinit();

    // Unregister event handlers
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
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

// Check if WiFi is connected
bool wifi_is_connected(void) {
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

// Wait for WiFi connection with timeout
bool wifi_wait_for_connection(uint32_t timeout_ms) {
    ESP_LOGI(TAG, "Waiting for WiFi connection (timeout: %lu ms)", (unsigned long)timeout_ms);

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
        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
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

    ESP_LOGI(TAG, "Waiting for IPv6 address (timeout: %lu ms)", (unsigned long)timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_IPV6_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_IPV6_BIT) {
        return true;
    }

    ESP_LOGI(TAG, "IPv6 address wait timeout");
    return false;
}

// Register connect callback
void wifi_register_connect_callback(void (*callback)(void)) {
    s_connect_callback = callback;
}

// Register disconnect callback
void wifi_register_disconnect_callback(void (*callback)(void)) {
    s_disconnect_callback = callback;
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

    // Call connect callback if registered
    if (s_connect_callback != NULL) {
        s_connect_callback();
    }
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
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
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_CONNECTED_IPV6_BIT);
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);

                // Call disconnect callback if registered
                if (s_disconnect_callback != NULL) {
                    s_disconnect_callback();
                }

                // Check if we've reached the maximum number of reconnection attempts
                if (s_reconnect_attempts >= MAX_RECONNECT_ATTEMPTS && !s_connection_given_up) {
                    ESP_LOGW(TAG, "Maximum reconnection attempts (%d) reached, giving up", MAX_RECONNECT_ATTEMPTS);
                    s_connection_given_up = true;
                    // We'll continue in AP mode only at this point
                } else if (!s_connection_given_up) {
                    // Only try to reconnect if we haven't given up yet
                    ESP_LOGI(TAG, "WiFi disconnected, trying to reconnect... (attempt %d/%d)",
                             s_reconnect_attempts, MAX_RECONNECT_ATTEMPTS);
                    esp_wifi_connect();
                }
                break;
#if ENABLE_AP_MODE
            case WIFI_EVENT_AP_STACONNECTED:
                {
                    wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
                    ESP_LOGI(TAG, "Station joined, AID=%d", event->aid);
                }
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                {
                    wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
                    ESP_LOGI(TAG, "Station left, AID=%d", event->aid);
                }
                break;
#endif
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
                {
                    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                    ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
                    handle_successful_ip_acquisition();
                }
                break;
            case IP_EVENT_GOT_IP6:
                {
                    ip_event_got_ip6_t* event = (ip_event_got_ip6_t*) event_data;
                    ip6_addr_t *addr = (ip6_addr_t *)&event->ip6_info.ip;
                    ESP_LOGI(TAG, "Got IPv6 address: " IPV6STR, IPV62STR(event->ip6_info.ip));

                    if (ip6_addr_isglobal(addr)) {
                        ESP_LOGI(TAG, "IPv6 address acquired");
                        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_IPV6_BIT);
                        handle_successful_ip_acquisition();
                    } else {
                        ESP_LOGI(TAG, "IPv6 address is not global, waiting...");
                    }
                }
                break;
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
            // ESP_LOGI(TAG, "WiFi reconnected successfully, resetting disconnect counter");
            s_wifi_disconnect_counter = 0;
        }
        return;
    }

    // WiFi is not connected, increment counter
    s_wifi_disconnect_counter++;
    ESP_LOGW(TAG, "WiFi Health check. Disconnect count: %d", s_wifi_disconnect_counter);

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

void wifi_connect(void) {
    char saved_ssid[33] = {0};
    nvs_get_ssid(saved_ssid, sizeof(saved_ssid));
    
    if (strlen(saved_ssid) == 0) {
        ESP_LOGI(TAG, "No SSID configured, not connecting");
        return;
    }

    // Reset reconnection counter and state
    s_reconnect_attempts = 0;
    s_connection_given_up = false;

    // Configure STA with the saved credentials
    wifi_config_t sta_config = {0};
    nvs_get_ssid((char *)sta_config.sta.ssid, sizeof(sta_config.sta.ssid));
    nvs_get_password((char *)sta_config.sta.password, sizeof(sta_config.sta.password));

    ESP_LOGI(TAG, "Connecting to SSID: %s", saved_ssid);

    // Set the STA configuration
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi STA config: %s", esp_err_to_name(err));
        return;
    }

    // Check WiFi state before connecting
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi mode");
        return;
    }

    // Connect to the AP - handle errors gracefully
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi connect failed: %s. Will retry automatically.", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "WiFi connect command sent successfully");
    }
}

void wifi_apply_power_save(void) {
    int power_save_mode = nvs_get_wifi_power_save();
    ESP_LOGI(TAG, "Setting WiFi Power Save Mode to %d...", power_save_mode);
    esp_wifi_set_ps((wifi_ps_type_t)power_save_mode);
}
