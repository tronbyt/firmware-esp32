#include "WifiManagerEsp32.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "customEvents.hpp"
#include <memory>

#include "wifi_manager_wrapper.h"

static const char* TAG = "wifi_manager_wrapper";
static std::unique_ptr<WifiManagerEsp32> wifiManager;

// Flag to indicate if WiFi is connected
static bool wifi_connected = false;

// Event handler for WiFi events
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(TAG, "WiFi connected to AP");
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI(TAG, "WiFi disconnected from AP");
            wifi_connected = false;

            // If we have credentials, try to reconnect
            if (wifiManager && wifiManager->credentials_opt.has_value()) {
                ESP_LOGI(TAG, "Trying to reconnect to WiFi...");
                esp_wifi_connect();
            }
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            ESP_LOGI(TAG, "Client disconnected from AP");
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "WiFi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            wifi_connected = true;

            // Don't turn off AP mode immediately - this might interfere with mDNS
            // Just mark as connected and let the application decide when to turn off AP
            ESP_LOGI(TAG, "WiFi connected successfully with IP.");
        }
    } else if (event_base == CUSTOM_EVENTS) {
        if (event_id == CREDENTIALS_AQUIRED) {
            ESP_LOGI(TAG, "WiFi credentials acquired");
            // Try to connect to WiFi with the new credentials
            if (wifiManager) {
                ESP_LOGI(TAG, "Trying to connect to WiFi with new credentials");
                esp_wifi_connect();
            }
        }
    }
}

extern "C" {

void wifi_manager_init(void) {
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

    // Register our event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(CUSTOM_EVENTS, CREDENTIALS_AQUIRED, &wifi_event_handler, NULL));

    // Initialize WiFiManager with custom config
    WifiManagerIdfConfig config;
    // Set a more visible SSID
    config.ssid = "TIDBYT-SETUP";
    // Don't keep AP mode on after connecting to WiFi
    config.shouldKeepAP = false;

    // Set log level to debug
    esp_log_level_set("*", ESP_LOG_DEBUG);

    // Create the WiFiManager instance
    wifiManager = std::make_unique<WifiManagerEsp32>(config);

    // The WifiManagerEsp32 constructor will automatically:
    // 1. Try to read credentials from SPIFFS
    // 2. If credentials are found, it will try to connect to WiFi
    // 3. If credentials are not found or connection fails, it will start AP mode

    // Explicitly start the AP mode and server
    wifiManager->setupWiFi(true, true);
    wifiManager->setupServerAndDns();

    ESP_LOGI(TAG, "WiFi setup complete!");
}

bool wifi_manager_is_connected(void) {
    // First check the wifi_connected flag set by the event handler
    if (wifi_connected) {
        return true;
    }

    // If the flag is not set, check if we're connected using the ESP-IDF API
    if (wifiManager) {
        // Check if we have credentials
        if (wifiManager->credentials_opt.has_value()) {
            // Check if the WiFi is connected
            wifi_ap_record_t ap_info;
            esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Connected to WiFi SSID: %s", ap_info.ssid);
                wifi_connected = true;
                return true;
            }

            // Also check the IP address
            esp_netif_ip_info_t ip_info;
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif) {
                if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                    if (ip_info.ip.addr != 0) {
                        ESP_LOGI(TAG, "Connected to WiFi with IP: " IPSTR, IP2STR(&ip_info.ip));
                        wifi_connected = true;
                        return true;
                    }
                }
            }

            // Check if we're in the process of connecting
            if (wifiManager->staStarted_opt.has_value() && wifiManager->staStarted_opt.value()) {
                ESP_LOGI(TAG, "WiFi connection in progress...");
                // Try to connect explicitly
                esp_wifi_connect();
            }
        }
    }
    return false;
}

void wifi_manager_get_mac(uint8_t* mac) {
    esp_wifi_get_mac(WIFI_IF_STA, mac);
}

void wifi_manager_start_ap(void) {
    if (wifiManager) {
        // If we're already connected to WiFi, don't start AP mode
        if (wifi_connected) {
            ESP_LOGI(TAG, "Already connected to WiFi, not starting AP mode");
            return;
        }

        ESP_LOGI(TAG, "Explicitly starting AP mode and web server");

        // Stop WiFi completely
        esp_wifi_stop();

        // Create a default STA interface if it doesn't exist
        esp_netif_create_default_wifi_sta();

        // Set up WiFi in AP+STA mode and start the server
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

        // If we have credentials, set them
        if (wifiManager->credentials_opt.has_value()) {
            wifi_config_t sta_config = {};
            auto ssid = wifiManager->credentials_opt.value()["ssid"].c_str();
            auto password = wifiManager->credentials_opt.value()["password"].c_str();
            strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
            strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
            ESP_LOGI(TAG, "Using saved credentials for SSID: %s", ssid);
        }

        // Start WiFi
        ESP_ERROR_CHECK(esp_wifi_start());

        // If we have credentials, try to connect
        if (wifiManager->credentials_opt.has_value()) {
            ESP_LOGI(TAG, "Trying to connect to WiFi...");
            esp_wifi_connect();
        }

        // Start the server and DNS
        wifiManager->setupServerAndDns();

        ESP_LOGI(TAG, "AP mode and web server should be running now");
        ESP_LOGI(TAG, "Connect to the 'TIDBYT-SETUP' WiFi network");
        ESP_LOGI(TAG, "Then navigate to http://4.3.2.1 in your browser");
    }
}

} // extern "C"
