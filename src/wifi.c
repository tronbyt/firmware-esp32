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

#define TAG "WIFI"

// Default AP configuration
#define DEFAULT_AP_SSID "TRON-CONFIG"
#define DEFAULT_AP_PASSWORD ""

// NVS namespace and keys
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"
#define NVS_KEY_IMAGE_URL "image_url"

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Maximum string lengths
#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64
#define MAX_URL_LEN 128

// Maximum number of reconnection attempts before giving up
#define MAX_RECONNECT_ATTEMPTS 10

// Static variables
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static httpd_handle_t s_server = NULL;
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

// Callback functions
static void (*s_connect_callback)(void) = NULL;
static void (*s_disconnect_callback)(void) = NULL;

// HTML for the configuration page
const char *s_html_page_template =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<title>Tronbyt WiFi Setup</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }"
    "h1 { color: #333; }"
    ".form-container { max-width: 400px; margin: 0 auto; }"
    ".form-group { margin-bottom: 15px; }"
    "label { display: block; margin-bottom: 5px; font-weight: bold; }"
    "input[type='text'], input[type='password'] { width: 100%; padding: 8px; "
    "box-sizing: border-box; }"
    "button { background-color: #4CAF50; color: white; padding: 10px 15px; "
    "border: none; cursor: pointer; }"
    "button:hover { background-color: #45a049; }"
    ".networks { margin-top: 20px; }"
    "</style>"
    "</head>"
    "<body>"
    "<div class='form-container'>"
    "<h1>Tronbyt WiFi Setup</h1>"
    "<form action='/save' method='post' "
    "enctype='application/x-www-form-urlencoded'>"
    "<div class='form-group'>"
    "<label for='ssid'>WiFi Network Name (2.4Ghz Only) :</label>"
    "<input type='text' id='ssid' name='ssid' maxlength='32'>"
    "</div>"
    "<div class='form-group'>"
    "<label for='password'>WiFi Password:</label>"
    "<input type='text' id='password' name='password' maxlength='64'>"
    "</div>"
    "<div class='form-group'>"
    "<label for='image_url'>Image URL:</label>"
    "<input type='text' id='image_url' name='image_url' maxlength='128' value='%s'>"
    "( If modifying Image URL reboot Tronbyt after saving. )"
    "</div>"
    "<button type='submit'>Save and Connect</button>"
    "</form>"
    "</div>"
    "</body>"
    "</html>";

static char s_html_page[4096];  // Make sure this is large enough

// Success page HTML
static const char *s_success_html = "<!DOCTYPE html>"
"<html>"
"<head>"
"<title>WiFi Configuration Saved</title>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
"body { font-family: Arial, sans-serif; margin: 0; padding: 20px; text-align: center; }"
"h1 { color: #4CAF50; }"
"p { margin-bottom: 20px; }"
"</style>"
"</head>"
"<body>"
"<h1>Configuration Saved!</h1>"
"<p>WiFi credentials and image URL have been saved.</p>"
"<p>The device will now attempt to connect to the WiFi network.</p>"
"<p>If you modified a previously saved Image URL please manually reboot your tron for the changes to take effect."
"<p>You can close this page.</p>"
"</body>"
"</html>";

// DNS server task handle
static TaskHandle_t s_dns_task_handle = NULL;

// Function prototypes
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t save_wifi_config_to_nvs(void);
static esp_err_t load_wifi_config_from_nvs(void);
static esp_err_t start_webserver(void);
static esp_err_t stop_webserver(void);
static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t save_handler(httpd_req_t *req);
static esp_err_t captive_portal_handler(httpd_req_t *req);
static void connect_to_ap(void);
static void url_decode(char *str);
static void dns_server_task(void *pvParameters);
static void start_dns_server(void);
static void stop_dns_server(void);
static bool has_saved_config = false;
    // Initialize WiFi
int wifi_initialize(const char *ssid, const char *password) {
  ESP_LOGI(TAG, "Initializing WiFi");

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
  s_ap_netif = esp_netif_create_default_wifi_ap();

  // Configure AP IP address to 10.10.0.1
  esp_netif_ip_info_t ip_info;
  IP4_ADDR(&ip_info.ip, 10, 10, 0, 1);
  IP4_ADDR(&ip_info.gw, 10, 10, 0, 1);
  IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

  // Stop DHCP server before changing IP
  esp_netif_dhcps_stop(s_ap_netif);

  // Set the new IP address
  esp_err_t err = esp_netif_set_ip_info(s_ap_netif, &ip_info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set AP IP info: %s", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "AP IP address set to 10.10.0.1");
  }

  // Start DHCP server with new configuration
  esp_netif_dhcps_start(s_ap_netif);

  // Initialize WiFi with default config
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Register event handlers
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
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

  // Start WiFi
  ESP_LOGI(TAG, "Starting WiFi");

  // Set WiFi mode to AP+STA
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

  // Configure AP with explicit settings
  wifi_config_t ap_config = {0};
  strcpy((char *)ap_config.ap.ssid, DEFAULT_AP_SSID);
  // strcpy((char *)ap_config.ap.password, DEFAULT_AP_PASSWORD);
  ap_config.ap.ssid_len = strlen(DEFAULT_AP_SSID);

  // Pick a random WiFi channel (1-11 for 2.4GHz)
  uint8_t random_channel = (esp_random() % 11) + 1;
  ap_config.ap.channel = random_channel;

  ap_config.ap.max_connection = 4;
  ap_config.ap.authmode = WIFI_AUTH_OPEN;
  ap_config.ap.beacon_interval = 100;  // Default beacon interval
  // failure_retry_cnt ??

      ESP_LOGI(TAG, "Setting AP SSID: %s on channel %d", DEFAULT_AP_SSID, random_channel);
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

  // Start WiFi
  ESP_ERROR_CHECK(esp_wifi_start());

  // Wait for AP to start
  vTaskDelay(pdMS_TO_TICKS(500));

  // Start the web server
  start_webserver();

  // Only attempt to connect if we have valid saved credentials
  if (has_saved_config && strlen(s_wifi_ssid) > 0) {
    ESP_LOGI(TAG, "Attempting to connect with saved/hardcoded credentials");
    connect_to_ap();
  } else {
    ESP_LOGI(TAG,
             "No valid WiFi credentials available, starting in AP mode only");
    // Reset any previous connection attempts
    s_reconnect_attempts = MAX_RECONNECT_ATTEMPTS;
    s_connection_given_up = true;
  }

  ESP_LOGI(TAG, "WiFi initialized successfully");
  return 0;
}

// Shutdown the AP by switching wifi mode to STA
void wifi_shutdown_ap(TimerHandle_t xTimer) {
  ESP_LOGI(TAG, "Shutting down config portal");
  stop_webserver();
  esp_wifi_set_mode(WIFI_MODE_STA);
}

// Shutdown WiFi
void wifi_shutdown() {
    // Stop the web server if it's running
    if (s_server != NULL) {
        stop_webserver();
    }

    // Stop WiFi
    esp_wifi_stop();
    esp_wifi_deinit();

    // Unregister event handlers
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);

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

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            // STA started, reset reconnection counter and try to connect
            s_reconnect_attempts = 0;
            s_connection_given_up = false;
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            // Increment reconnection counter
            s_reconnect_attempts++;

            // Clear connection bit and set fail bit
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
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
        } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
            ESP_LOGI(TAG, "Station joined, AID=%d", event->aid);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
            ESP_LOGI(TAG, "Station left, AID=%d", event->aid);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));

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

// Start the web server
static esp_err_t start_webserver(void) {
    if (s_server != NULL) {
        ESP_LOGI(TAG, "Web server already started");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 8;
    config.max_resp_headers = 16;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 8;
    config.max_resp_headers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;  // Add this for wildcard matching
    config.lru_purge_enable = true;  // Enable LRU purging for better memory management

    // We can't directly increase the max header size in this ESP-IDF version
    // Instead, we'll handle the 431 error gracefully in the handlers

    ESP_LOGI(TAG, "Starting web server on 10.10.0.1:%d", config.server_port);

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        return ESP_FAIL;
    }

    // URI handlers - order matters, specific routes first
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &root_uri);

    httpd_uri_t save_uri = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &save_uri);

    // Common captive portal detection URLs for various operating systems
    // iOS and macOS
    httpd_uri_t hotspot_detect_uri = {
        .uri = "/hotspot-detect.html",
        .method = HTTP_GET,
        .handler = captive_portal_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &hotspot_detect_uri);

    // Android
    httpd_uri_t generate_204_uri = {
        .uri = "/generate_204",
        .method = HTTP_GET,
        .handler = captive_portal_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &generate_204_uri);

    // Windows
    httpd_uri_t ncsi_uri = {
        .uri = "/ncsi.txt",
        .method = HTTP_GET,
        .handler = captive_portal_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &ncsi_uri);

    // Catch-all wildcard handler for any other requests
    httpd_uri_t wildcard_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = captive_portal_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_server, &wildcard_uri);

    // Start DNS server for captive portal
    start_dns_server();

    return ESP_OK;
}

// Stop the web server
static esp_err_t stop_webserver(void) {
    if (s_server == NULL) {
        return ESP_OK;
    }

    // Stop DNS server
    stop_dns_server();

    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    return err;
}

// Root page handler
static esp_err_t root_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Injecting image url (%s) to html template", s_image_url);
    snprintf(s_html_page, sizeof(s_html_page), s_html_page_template, s_image_url);
    ESP_LOGI(TAG, "Serving root page");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, s_html_page, strlen(s_html_page));
    return ESP_OK;
}

// Save handler for form submission
static esp_err_t save_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Processing form submission");

    // Increase buffer size to handle larger form data
    char *buf = malloc(4096);  // Increased from 2048 to 4096
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for form data");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server Error");
        return ESP_FAIL;
    }

    int ret, remaining = req->content_len;

    if (remaining > 4095) {  // Updated to match new buffer size
        ESP_LOGE(TAG, "Form data too large: %d bytes", remaining);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Form data too large");
        free(buf);
        return ESP_FAIL;
    }

    // Read the form data
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Failed to receive form data");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to receive form data");
        free(buf);
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    ESP_LOGI(TAG, "Received form data (%d bytes)", ret);

    // Parse the form data
    char ssid[MAX_SSID_LEN + 1] = {0};
    char password[MAX_PASSWORD_LEN + 1] = {0};
    char image_url[MAX_URL_LEN + 1] = {0};

    // Simple parsing of form data (format: key1=value1&key2=value2&...)
    char *saveptr;
    char *token = strtok_r(buf, "&", &saveptr);
    while (token != NULL) {
        ESP_LOGI(TAG, "Processing token: %s", token);
        if (strncmp(token, "ssid=", 5) == 0) {
            strncpy(ssid, token + 5, MAX_SSID_LEN);
            ssid[MAX_SSID_LEN] = '\0';
        } else if (strncmp(token, "password=", 9) == 0) {
            strncpy(password, token + 9, MAX_PASSWORD_LEN);
            password[MAX_PASSWORD_LEN] = '\0';
        } else if (strncmp(token, "image_url=", 10) == 0) {
            strncpy(image_url, token + 10, MAX_URL_LEN);
            image_url[MAX_URL_LEN] = '\0';
        }
        token = strtok_r(NULL, "&", &saveptr);
    }

    // URL decode the values
    // This is a simple implementation and doesn't handle all cases
    for (int i = 0; i < strlen(ssid); i++) {
        if (ssid[i] == '+') ssid[i] = ' ';
    }
    for (int i = 0; i < strlen(password); i++) {
        if (password[i] == '+') password[i] = ' ';
    }
    for (int i = 0; i < strlen(image_url); i++) {
        if (image_url[i] == '+') image_url[i] = ' ';
    }

    // Properly decode URL-encoded characters like %20
    url_decode(ssid);
    url_decode(password);
    url_decode(image_url);

    ESP_LOGI(TAG, "Received SSID: %s, Image URL: %s", ssid, image_url);

    // Save the new configuration
    strncpy(s_wifi_ssid, ssid, MAX_SSID_LEN);
    strncpy(s_wifi_password, password, MAX_PASSWORD_LEN);
    if (strlen(image_url) < 6) {
        ESP_LOGI(TAG, "new image url is blank, leaving as is");
    } else {
        strncpy(s_image_url, image_url, MAX_URL_LEN);
    }
    // Free the buffer as we don't need it anymore
    free(buf);

    // Save to NVS
    save_wifi_config_to_nvs();

    // Send success response
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, s_success_html, strlen(s_success_html));

    // Connect to the new AP
    connect_to_ap();

    return ESP_OK;
}

// Connect to the configured AP
static void connect_to_ap(void) {
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
        // Don't crash on connection error - the WiFi event handler will retry
    } else {
        ESP_LOGI(TAG, "WiFi connect command sent successfully");
    }
}

// Helper function to decode URL-encoded strings
static void url_decode(char *str) {
    char *src = str;
    char *dst = str;

    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            // Convert hex characters to value
            char hex[3] = {src[1], src[2], 0};
            *dst = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
          *dst = ' ';
          src++;
        } else {
            *dst = *src;
            src++;
        }
        dst++;
    }
    *dst = '\0'; // Null-terminate the decoded string
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
    ESP_LOGW(TAG, "WiFi Health check. Disconnect count: %d/*", s_wifi_disconnect_counter);

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

    // If counter reaches threshold, reboot the system
    // if (s_wifi_disconnect_counter >= 15) {
    //     ESP_LOGE(TAG, "WiFi disconnected for 15 consecutive checks. Rebooting system...");
    //     // Wait a moment before rebooting
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    //     esp_restart();
    // }
}

// DNS server implementation for captive portal
#define DNS_PORT 53
#define DNS_MAX_LEN 512

// DNS header structure
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

// DNS server task - responds to all DNS queries with AP IP
static void dns_server_task(void *pvParameters) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DNS_PORT);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server started on port 53");

    char rx_buffer[DNS_MAX_LEN];
    char tx_buffer[DNS_MAX_LEN];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while (1) {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                          (struct sockaddr *)&client_addr, &client_addr_len);

        if (len < 0) {
            ESP_LOGE(TAG, "DNS recvfrom failed");
            break;
        }

        if (len < sizeof(dns_header_t)) {
            continue; // Invalid DNS packet
        }

        dns_header_t *header = (dns_header_t *)rx_buffer;

        // Only respond to queries (QR bit = 0)
        if ((ntohs(header->flags) & 0x8000) != 0) {
            continue;
        }

        // Build response
        memcpy(tx_buffer, rx_buffer, len);
        dns_header_t *resp_header = (dns_header_t *)tx_buffer;

        // Set response flags: QR=1 (response), AA=1 (authoritative)
        resp_header->flags = htons(0x8400);

        int response_len = len;
        int answers_added = 0;

        // For captive portal, we only need to answer the first question
        // Most DNS queries only have one question anyway
        uint16_t num_questions = ntohs(header->qdcount);
        if (num_questions > 0 && response_len + 16 < DNS_MAX_LEN) {
            // Answer: pointer to question name (0xC00C), type A, class IN, TTL, length 4, IP
            uint8_t answer[] = {
                0xC0, 0x0C,           // Pointer to domain name in question
                0x00, 0x01,           // Type A
                0x00, 0x01,           // Class IN
                0x00, 0x00, 0x00, 0x3C, // TTL (60 seconds)
                0x00, 0x04,           // Data length (4 bytes)
                10, 10, 0, 1          // IP address 10.10.0.1
            };

            memcpy(tx_buffer + response_len, answer, sizeof(answer));
            response_len += sizeof(answer);
            answers_added = 1;
        }

        // Set the actual answer count
        resp_header->ancount = htons(answers_added);

        // Send response
        sendto(sock, tx_buffer, response_len, 0,
               (struct sockaddr *)&client_addr, client_addr_len);
    }

    close(sock);
    vTaskDelete(NULL);
}

// Start DNS server
static void start_dns_server(void) {
    if (s_dns_task_handle != NULL) {
        ESP_LOGW(TAG, "DNS server already running");
        return;
    }

    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &s_dns_task_handle);
}

// Stop DNS server
static void stop_dns_server(void) {
    if (s_dns_task_handle != NULL) {
        vTaskDelete(s_dns_task_handle);
        s_dns_task_handle = NULL;
        ESP_LOGI(TAG, "DNS server stopped");
    }
}

// Captive portal handler - redirects all requests to config page
static esp_err_t captive_portal_handler(httpd_req_t *req) {
    char *host_buf = NULL;
    bool serve_directly = false;

    // Try to get the Host header
    size_t host_len = httpd_req_get_hdr_value_len(req, "Host");
    if (host_len > 0) {
        host_buf = malloc(host_len + 1);
        if (host_buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for Host header");
            // Continue without host check - will redirect
        } else {
            if (httpd_req_get_hdr_value_str(req, "Host", host_buf, host_len + 1) != ESP_OK) {
                // Failed to get header value, free buffer and continue
                free(host_buf);
                host_buf = NULL;
            }
        }
    }

    // Check if this is already our IP address
    if (host_buf != NULL &&
        (strcmp(host_buf, "10.10.0.1") == 0 || strstr(host_buf, "10.10.0.1") != NULL)) {
        serve_directly = true;
    }

    // Clean up host buffer
    if (host_buf != NULL) {
        free(host_buf);
        host_buf = NULL;
    }

    // Serve the config page directly or redirect
    if (serve_directly) {
        return root_handler(req);
    }

    // Redirect to our config page
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://10.10.0.1/");
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}
