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

#define TAG "WIFI"

// NVS namespace and keys
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"
#define NVS_KEY_IMAGE_URL "image_url"

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_CONNECTED_IPV6_BIT BIT2

// Maximum string lengths
#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64
#define MAX_URL_LEN 128

// Maximum number of reconnection attempts before giving up
#define MAX_RECONNECT_ATTEMPTS 10

// Static variables
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_sta_netif = NULL;
static void (*s_config_callback)(void) = NULL;

// WiFi credentials
static char s_wifi_ssid[MAX_SSID_LEN + 1] = {0};
static char s_wifi_password[MAX_PASSWORD_LEN + 1] = {0};
static char s_image_url[MAX_URL_LEN + 1] = {0};

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
static esp_err_t save_wifi_config_to_nvs(void);
static esp_err_t load_wifi_config_from_nvs(void);

static bool has_saved_config = false;


// Initialize WiFi
int wifi_initialize(const char *ssid, const char *password) {
  ESP_LOGI(TAG, "Initializing WiFi");

#if !ENABLE_AP_MODE
  ESP_LOGI(TAG, "AP mode disabled via secrets; starting without config portal");
#endif

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGI(TAG, "Erasing NVS flash");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Create event group
  s_wifi_event_group = xEventGroupCreate();

  // Initialize TCP/IP adapter
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Create default STA and AP network interfaces
  s_sta_netif = esp_netif_create_default_wifi_sta();
#if ENABLE_AP_MODE
  ap_init_netif();
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

  // Load saved configuration from NVS
  has_saved_config = (load_wifi_config_from_nvs() == ESP_OK);

  // If no saved configuration, try to use the hardcoded credentials
  if (!has_saved_config) {
    ESP_LOGI(TAG,
             "No saved WiFi configuration found, using hardcoded credentials");

    // Force the compiler to include these strings in the binary
    char placeholder_ssid[MAX_SSID_LEN + 1] = WIFI_SSID;  // PATCH:SSID
    char placeholder_password[MAX_PASSWORD_LEN + 1] = WIFI_PASSWORD;  // PATCH:PASS
    char placeholder_url[MAX_URL_LEN + 1] = REMOTE_URL;

    ESP_LOGI(TAG, "Hardcoded WIFI_SSID: %s", placeholder_ssid);
    ESP_LOGI(TAG, "Hardcoded WIFI_PASSWORD: %s", placeholder_password);
    ESP_LOGI(TAG, "Hardcoded REMOTE_URL: %s", placeholder_url);

    // Check if SSID contains placeholder text or is empty

    if (strstr(placeholder_ssid, "Xplaceholder") != NULL) {
      ESP_LOGW(TAG,
               "WIFI_SSID contains placeholder text or is empty, not using "
               "hardcoded credentials");
    } else {
      // Save the hardcoded credentials to our internal variables
      strncpy(s_wifi_ssid, placeholder_ssid, MAX_SSID_LEN);
      s_wifi_ssid[MAX_SSID_LEN] = '\0';

      // Check if password contains placeholder text
      if (strstr(placeholder_password, "Xplaceholder") != NULL) {
        ESP_LOGW(TAG, "WIFI_PASSWORD contains placeholder text, not using it");
        s_wifi_password[0] = '\0';
      } else {
        strncpy(s_wifi_password, placeholder_password, MAX_PASSWORD_LEN);
        s_wifi_password[MAX_PASSWORD_LEN] = '\0';
      }

      // Also load the hardcoded REMOTE_URL as the image URL if available
      // Check if REMOTE_URL contains placeholder text or is empty
      if (strstr(placeholder_url, "Xplaceholder") != NULL) {
        ESP_LOGW(
            TAG,
            "REMOTE_URL contains placeholder text or is empty, not using it");
        s_image_url[0] = '\0';
      } else {
        ESP_LOGI(TAG, "Using hardcoded REMOTE_URL: %s", placeholder_url);
        strncpy(s_image_url, placeholder_url, MAX_URL_LEN);
        s_image_url[MAX_URL_LEN] = '\0';
      }

      // Save to NVS for future use only if we have valid credentials
      if (strlen(s_wifi_ssid) > 0 && strlen(s_wifi_password) > 0) {
        save_wifi_config_to_nvs();
        has_saved_config = true;
        ESP_LOGI(TAG, "Saved hardcoded credentials to NVS");
      } else {
        ESP_LOGW(TAG, "Not saving incomplete WiFi credentials to NVS");
        has_saved_config = false;
      }
    }
  }

#if ENABLE_AP_MODE
  ap_configure();
#else
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
#endif

  // Configure STA with credentials if available
  if (strlen(s_wifi_ssid) > 0) {
      wifi_config_t sta_config = {0};
      strncpy((char *)sta_config.sta.ssid, s_wifi_ssid, sizeof(sta_config.sta.ssid) - 1);
      strncpy((char *)sta_config.sta.password, s_wifi_password, sizeof(sta_config.sta.password) - 1);
      ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
      ESP_LOGI(TAG, "Configured STA with SSID: %s", s_wifi_ssid);
  }

  // Start WiFi
  ESP_ERROR_CHECK(esp_wifi_start());

  /* Apply WiFi Power Save Mode from configuration */
  ESP_LOGI(TAG, "Setting WiFi Power Save Mode to %d...", WIFI_POWER_SAVE_MODE);
  esp_wifi_set_ps((wifi_ps_type_t)WIFI_POWER_SAVE_MODE);

  // Wait for AP to start
#if ENABLE_AP_MODE
  vTaskDelay(pdMS_TO_TICKS(500));

  // Start the web server
  ap_start();
#endif

  // Only attempt to connect if we have valid saved credentials
  if (!(has_saved_config && strlen(s_wifi_ssid) > 0)) {
#if ENABLE_AP_MODE
    ESP_LOGI(TAG,
             "No valid WiFi credentials available, starting in AP mode only");
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

// Shutdown WiFi
void wifi_shutdown() {
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
    if (!has_saved_config) {
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

#if PREFER_IPV6
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
#endif

// Get the current image URL
const char* wifi_get_image_url(void) {
    return (strlen(s_image_url) > 0) ? s_image_url : NULL;
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

// Save WiFi configuration to NVS
static esp_err_t save_wifi_config_to_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    if (s_wifi_ssid[0] != '\0') {
      err = nvs_set_str(nvs_handle, NVS_KEY_SSID, s_wifi_ssid);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving SSID to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
      }
    }

    if (s_wifi_password[0] != '\0') {
      err = nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, s_wifi_password);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving password to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
      }
    }

    if (s_image_url[0] != '\0') {
      err = nvs_set_str(nvs_handle, NVS_KEY_IMAGE_URL, s_image_url);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving image URL to NVS: %s",
                 esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
      }
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);

    // Call config callback if registered and save was successful
    if (err == ESP_OK && s_config_callback != NULL) {
        s_config_callback();
    }

    return err;
}

// Load WiFi configuration from NVS
static esp_err_t load_wifi_config_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved WiFi configuration found");
        return err;
    }

    size_t required_size = MAX_SSID_LEN;
    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, s_wifi_ssid, &required_size);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved SSID found");
        nvs_close(nvs_handle);
        return err;
    }

    required_size = MAX_PASSWORD_LEN;
    err = nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, s_wifi_password, &required_size);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved password found");
        // Clear SSID if password is not found
        memset(s_wifi_ssid, 0, sizeof(s_wifi_ssid));
        nvs_close(nvs_handle);
        return err;
    }

    required_size = MAX_URL_LEN;
    err = nvs_get_str(nvs_handle, NVS_KEY_IMAGE_URL, s_image_url, &required_size);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved image URL found");
        // This is not a critical error, just set empty URL
        memset(s_image_url, 0, sizeof(s_image_url));
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Loaded WiFi configuration - SSID: %s, Image URL: %s", s_wifi_ssid, s_image_url);
    return ESP_OK;
}

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
    if (strlen(s_wifi_ssid) > 0) {
        ESP_LOGI(TAG, "Reconnecting in Health check...");
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WiFi reconnect attempt failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "No SSID configured, cannot reconnect");
    }
}

esp_err_t wifi_save_config(const char *ssid, const char *password, const char *image_url) {
    if (ssid) {
        strncpy(s_wifi_ssid, ssid, MAX_SSID_LEN);
        s_wifi_ssid[MAX_SSID_LEN] = '\0';
    }
    if (password) {
        strncpy(s_wifi_password, password, MAX_PASSWORD_LEN);
        s_wifi_password[MAX_PASSWORD_LEN] = '\0';
    }
    if (image_url) {
        strncpy(s_image_url, image_url, MAX_URL_LEN);
        s_image_url[MAX_URL_LEN] = '\0';
    }

    return save_wifi_config_to_nvs();
}

void wifi_connect(void) {
    if (strlen(s_wifi_ssid) == 0) {
        ESP_LOGI(TAG, "No SSID configured, not connecting");
        return;
    }

    // Reset reconnection counter and state
    s_reconnect_attempts = 0;
    s_connection_given_up = false;

    // Configure STA with the saved credentials
    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, s_wifi_ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, s_wifi_password, sizeof(sta_config.sta.password) - 1);

    ESP_LOGI(TAG, "Connecting to SSID: %s", s_wifi_ssid);

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

