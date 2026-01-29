#include <cJSON.h>
#include <ctype.h>  // For isdigit
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <webp/demux.h>

#include "ap.h"
#include "display.h"
#include "esp_sntp.h"
#include "flash.h"
#include "gfx.h"
#include "nvs_settings.h"
#include "ota.h"
#include "remote.h"
#include "sdkconfig.h"
#include "sntp.h"
#include "syslog.h"
#include "version.h"
#include "wifi.h"

#if CONFIG_BUTTON_PIN >= 0
#include <driver/gpio.h>
#endif

// Default URL if none is provided through WiFi manager
#define DEFAULT_URL "http://URL.NOT.SET/"
#define WEBSOCKET_PROTOCOL_VERSION 1

#ifndef CONFIG_REFRESH_INTERVAL_SECONDS
#define CONFIG_REFRESH_INTERVAL_SECONDS 10
#endif

static const char* TAG = "main";
int32_t isAnimating = 1;
static int32_t app_dwell_secs = CONFIG_REFRESH_INTERVAL_SECONDS;
// main buffer downloaded webp data
static uint8_t* webp;
// Flag to track oversize websocket messages
static bool websocket_oversize_detected = false;

static bool use_websocket = false;
static esp_websocket_client_handle_t ws_handle;
static EventGroupHandle_t s_ws_event_group;
#define WS_CONNECTED_BIT BIT0

static bool button_boot = false;
static bool first_ws_image_received = false;
static bool config_received = false;

static void config_saved_callback(void) {
  config_received = true;
  ESP_LOGI(TAG, "Configuration saved - signaling main task");
}

static void ota_task_entry(void* pvParameter) {
  char* url = (char*)pvParameter;
  run_ota(url);
  free(url);  // Free the duplicated string
  vTaskDelete(NULL);
}

// Globals for WebSocket reassembly
static size_t ws_accumulated_len = 0;

static esp_err_t send_client_info(void) {
  esp_err_t ret = ESP_OK;
  uint8_t mac[6];
  char ssid[33] = {0};
  char hostname[33] = {0};
  char syslog_addr[MAX_SYSLOG_ADDR_LEN + 1] = {0};
  char sntp_server[MAX_SNTP_SERVER_LEN + 1] = {0};
  nvs_get_ssid(ssid, sizeof(ssid));
  nvs_get_hostname(hostname, sizeof(hostname));
  nvs_get_syslog_addr(syslog_addr, sizeof(syslog_addr));
  nvs_get_sntp_server(sntp_server, sizeof(sntp_server));
  const char* image_url = nvs_get_image_url();
  if (image_url == NULL) image_url = "";

  cJSON* root = cJSON_CreateObject();
  if (root) {
    cJSON* ci = cJSON_AddObjectToObject(root, "client_info");
    if (ci) {
      cJSON_AddStringToObject(ci, "firmware_version", FIRMWARE_VERSION);
      cJSON_AddStringToObject(ci, "firmware_type", "ESP32");
      cJSON_AddNumberToObject(ci, "protocol_version",
                              WEBSOCKET_PROTOCOL_VERSION);

      if (wifi_get_mac(mac) == 0) {
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        cJSON_AddStringToObject(ci, "mac", mac_str);
      } else {
        ESP_LOGW(TAG,
                 "Failed to get MAC address; sending client info without MAC.");
      }

      cJSON_AddStringToObject(ci, "ssid", ssid);
      cJSON_AddStringToObject(ci, "hostname", hostname);
      cJSON_AddStringToObject(ci, "syslog_addr", syslog_addr);
      cJSON_AddStringToObject(ci, "sntp_server", sntp_server);
      cJSON_AddStringToObject(ci, "image_url", image_url);
      cJSON_AddBoolToObject(ci, "swap_colors", nvs_get_swap_colors());
      cJSON_AddNumberToObject(ci, "wifi_power_save", nvs_get_wifi_power_save());
      cJSON_AddBoolToObject(ci, "skip_display_version",
                            nvs_get_skip_display_version());
      cJSON_AddBoolToObject(ci, "ap_mode", nvs_get_ap_mode());
      cJSON_AddBoolToObject(ci, "prefer_ipv6", nvs_get_prefer_ipv6());

      char* json_str = cJSON_PrintUnformatted(root);
      if (json_str) {
        ESP_LOGI(TAG, "Sending client info: %s", json_str);
        int sent = esp_websocket_client_send_text(
            ws_handle, json_str, strlen(json_str), portMAX_DELAY);
        if (sent < 0) {
          ESP_LOGE(TAG, "Failed to send client info: %d", sent);
          ret = ESP_FAIL;
        }
        free(json_str);
      } else {
        ret = ESP_ERR_NO_MEM;
      }
    } else {
      ret = ESP_FAIL;  // Failed to add object
    }
    cJSON_Delete(root);
  } else {
    ret = ESP_ERR_NO_MEM;
  }
  return ret;
}

static void websocket_event_handler(void* handler_args, esp_event_base_t base,
                                    int32_t event_id, void* event_data) {
  esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;
  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
      xEventGroupSetBits(s_ws_event_group, WS_CONNECTED_BIT);
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
      draw_error_indicator_pixel();
      break;
    case WEBSOCKET_EVENT_DATA:
      // Process text messages (op_code == 1)
      if (data->op_code == 1 && data->data_len > 0) {
        // Check if this is a complete message or just a fragment
        // For text, we assume it's small enough to not be fragmented or we just
        // take what we get for now (Improving text handling to support
        // fragmentation is separate, but config is usually small)
        bool is_complete =
            (data->payload_offset + data->data_len >= data->payload_len);

        if (is_complete) {
          // Ensure null-termination for cJSON
          char* json_str = malloc(data->data_len + 1);
          if (json_str) {
            memcpy(json_str, data->data_ptr, data->data_len);
            json_str[data->data_len] = '\0';
            cJSON* root = cJSON_Parse(json_str);
            free(json_str);

            if (root) {
              bool settings_changed = false;

              // Check for "immediate"
              cJSON* immediate_item = cJSON_GetObjectItem(root, "immediate");
              if (cJSON_IsBool(immediate_item) &&
                  cJSON_IsTrue(immediate_item)) {
                ESP_LOGD(TAG,
                         "Interrupting current animation to load queued image");
                isAnimating = -1;
              }

              // Check for "dwell_secs"
              cJSON* dwell_item = cJSON_GetObjectItem(root, "dwell_secs");
              if (cJSON_IsNumber(dwell_item)) {
                int dwell_value = dwell_item->valueint;
                if (dwell_value < 1) dwell_value = 1;
                if (dwell_value > 3600) dwell_value = 3600;
                app_dwell_secs = dwell_value;
                ESP_LOGD(TAG, "Updated dwell_secs to %" PRId32 " seconds",
                         app_dwell_secs);
              }

              // Check for "brightness"
              cJSON* brightness_item = cJSON_GetObjectItem(root, "brightness");
              if (cJSON_IsNumber(brightness_item)) {
                int brightness_value = brightness_item->valueint;
                if (brightness_value < DISPLAY_MIN_BRIGHTNESS)
                  brightness_value = DISPLAY_MIN_BRIGHTNESS;
                if (brightness_value > DISPLAY_MAX_BRIGHTNESS)
                  brightness_value = DISPLAY_MAX_BRIGHTNESS;
                display_set_brightness((uint8_t)brightness_value);
                ESP_LOGI(TAG, "Updated brightness to %d", brightness_value);
              }

              // Check for "ota_url"
              cJSON* ota_item = cJSON_GetObjectItem(root, "ota_url");
              if (cJSON_IsString(ota_item) && (ota_item->valuestring != NULL)) {
                char* ota_url = strdup(ota_item->valuestring);
                if (ota_url) {
                  ESP_LOGI(TAG, "OTA URL received via WS: %s", ota_url);
                  xTaskCreate(ota_task_entry, "ota_task", 8192, ota_url, 5,
                              NULL);
                }
              }

              // Check for "swap_colors"
              cJSON* swap_colors_item =
                  cJSON_GetObjectItem(root, "swap_colors");
              if (cJSON_IsBool(swap_colors_item)) {
                bool val = cJSON_IsTrue(swap_colors_item);
                nvs_set_swap_colors(val);
                ESP_LOGI(TAG, "Updated swap_colors to %d", val);
                settings_changed = true;
              }

              // Check for "wifi_power_save"
              cJSON* wifi_ps_item =
                  cJSON_GetObjectItem(root, "wifi_power_save");
              if (cJSON_IsNumber(wifi_ps_item)) {
                wifi_ps_type_t val = (wifi_ps_type_t)wifi_ps_item->valueint;
                nvs_set_wifi_power_save(val);
                ESP_LOGI(TAG, "Updated wifi_power_save to %d", val);
                settings_changed = true;
                wifi_apply_power_save();
              }

              // Check for "skip_display_version"
              cJSON* skip_ver_item =
                  cJSON_GetObjectItem(root, "skip_display_version");
              if (cJSON_IsBool(skip_ver_item)) {
                bool val = cJSON_IsTrue(skip_ver_item);
                nvs_set_skip_display_version(val);
                ESP_LOGI(TAG, "Updated skip_display_version to %d", val);
                settings_changed = true;
              }

              // Check for "ap_mode"
              cJSON* ap_mode_item = cJSON_GetObjectItem(root, "ap_mode");
              if (cJSON_IsBool(ap_mode_item)) {
                bool val = cJSON_IsTrue(ap_mode_item);
                nvs_set_ap_mode(val);
                ESP_LOGI(TAG, "Updated ap_mode to %d", val);
                settings_changed = true;
              }

              // Check for "prefer_ipv6"
              cJSON* prefer_ipv6_item =
                  cJSON_GetObjectItem(root, "prefer_ipv6");
              if (cJSON_IsBool(prefer_ipv6_item)) {
                bool val = cJSON_IsTrue(prefer_ipv6_item);
                nvs_set_prefer_ipv6(val);
                ESP_LOGI(TAG, "Updated prefer_ipv6 to %d", val);
                settings_changed = true;
              }

              // Check for "hostname"
              cJSON* hostname_item = cJSON_GetObjectItem(root, "hostname");
              if (cJSON_IsString(hostname_item) &&
                  (hostname_item->valuestring != NULL)) {
                const char* new_hostname = hostname_item->valuestring;
                if (strlen(new_hostname) > 0 && strlen(new_hostname) <= 32) {
                  nvs_set_hostname(new_hostname);
                  wifi_set_hostname(new_hostname);
                  ESP_LOGI(TAG, "Updated hostname to %s", new_hostname);
                  settings_changed = true;
                } else {
                  ESP_LOGW(TAG, "Invalid hostname received: %s", new_hostname);
                }
              }

              // Check for "syslog_addr"
              cJSON* syslog_addr_item =
                  cJSON_GetObjectItem(root, "syslog_addr");
              if (cJSON_IsString(syslog_addr_item) &&
                  (syslog_addr_item->valuestring != NULL)) {
                const char* new_addr = syslog_addr_item->valuestring;
                nvs_set_syslog_addr(new_addr);
                syslog_update_config(new_addr);
                ESP_LOGI(TAG, "Updated syslog_addr to %s", new_addr);
                settings_changed = true;
              }

              // Check for "sntp_server"
              cJSON* sntp_server_item =
                  cJSON_GetObjectItem(root, "sntp_server");
              if (cJSON_IsString(sntp_server_item) &&
                  (sntp_server_item->valuestring != NULL)) {
                const char* new_server = sntp_server_item->valuestring;
                nvs_set_sntp_server(new_server);
                // Note: SNTP reconfiguration usually requires restart or
                // re-init logic which we don't have exposed. But settings are
                // saved.
                ESP_LOGI(TAG, "Updated sntp_server to %s", new_server);
                settings_changed = true;
              }

              // Check for "image_url"
              cJSON* image_url_item = cJSON_GetObjectItem(root, "image_url");
              if (cJSON_IsString(image_url_item) &&
                  (image_url_item->valuestring != NULL)) {
                nvs_set_image_url(image_url_item->valuestring);
                ESP_LOGI(TAG, "Updated image_url to %s",
                         image_url_item->valuestring);
                settings_changed = true;
              }

              if (settings_changed) {
                esp_err_t err = nvs_save_settings();
                if (err == ESP_OK) {
                  send_client_info();
                } else {
                  ESP_LOGE(TAG, "Failed to save settings: %s",
                           esp_err_to_name(err));
                }
              }

              // Check for "reboot"
              cJSON* reboot_item = cJSON_GetObjectItem(root, "reboot");
              if (cJSON_IsBool(reboot_item) && cJSON_IsTrue(reboot_item)) {
                ESP_LOGI(TAG, "Reboot command received via WS");
                esp_restart();
              }

              cJSON_Delete(root);
            } else {
              ESP_LOGW(TAG, "Failed to parse WebSocket text message as JSON");
            }
          } else {
            ESP_LOGE(TAG, "Failed to allocate memory for JSON parsing");
          }
        }
      } else if (data->op_code == 2 || data->op_code == 0) {
        // Binary data (WebP image) or Continuation

        // Start of new message: Opcode 2 at Offset 0
        if (data->op_code == 2 && data->payload_offset == 0) {
          if (webp != NULL) {
            ESP_LOGW(TAG, "Discarding incomplete previous WebP buffer");
            free(webp);
            webp = NULL;
          }
          ws_accumulated_len = 0;
          websocket_oversize_detected = false;
        }

        // Skip if oversize detected
        if (websocket_oversize_detected) break;

        // If Opcode 0 (Continuation) but no buffer, ignore (orphan or text
        // continuation)
        if (data->op_code == 0 && webp == NULL) break;

        // Resize buffer
        size_t new_size = ws_accumulated_len + data->data_len;
        if (new_size > CONFIG_HTTP_BUFFER_SIZE_MAX) {
          ESP_LOGE(TAG, "WebP size (%zu bytes) exceeds max (%d)", new_size,
                   CONFIG_HTTP_BUFFER_SIZE_MAX);
          websocket_oversize_detected = true;
          if (gfx_display_asset("oversize") != 0) {
            ESP_LOGE(TAG, "Failed to display oversize graphic");
          }
          if (webp) {
            free(webp);
            webp = NULL;
          }
          ws_accumulated_len = 0;
          break;
        }

        uint8_t* new_buf = heap_caps_realloc(webp, new_size, MALLOC_CAP_SPIRAM);
        if (new_buf == NULL) {
          ESP_LOGE(TAG, "Failed to allocate memory (%zu bytes)", new_size);
          if (webp) {
            free(webp);
            webp = NULL;
          }
          ws_accumulated_len = 0;
          break;
        }
        webp = new_buf;

        // Append data
        memcpy(webp + ws_accumulated_len, data->data_ptr, data->data_len);
        ws_accumulated_len = new_size;

        // Check for completion
        // Frame is complete if we received the full payload of this frame
        bool frame_complete =
            (data->payload_offset + data->data_len >= data->payload_len);

        // Message is complete if this is the Final Frame (FIN) and we have all
        // of it
        if (data->fin && frame_complete) {
          ESP_LOGD(TAG, "WebP download complete (%zu bytes)",
                   ws_accumulated_len);

          // Queue the complete binary data as a WebP image
          // This will wait for the current animation to finish before loading
          gfx_update(webp, ws_accumulated_len, app_dwell_secs);

          if (!first_ws_image_received) {
            ESP_LOGI(
                TAG,
                "First WebSocket image received - interrupting boot animation");
            isAnimating = -1;
            first_ws_image_received = true;
          }

          // Do not free(webp) here; ownership is transferred to gfx
          webp = NULL;
          ws_accumulated_len = 0;
        }
      }

      break;
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGE(TAG, "WEBSOCKET_EVENT_ERROR");
      // Check if we have an incomplete WebP buffer
      if (webp != NULL) {
        ESP_LOGW(TAG,
                 "WebSocket error with incomplete WebP buffer - discarding");
        free(webp);
        webp = NULL;
      }
      draw_error_indicator_pixel();
      break;
  }
}

void app_main(void) {
  const char* image_url = NULL;

  // delete here for 5 seconds to allow for serial port to connect.
  ESP_LOGI(TAG, "App Main Start");

#if CONFIG_BUTTON_PIN >= 0
  // Configure button pin as input with pull-up
  gpio_config_t button_config = {.pin_bit_mask = (1ULL << CONFIG_BUTTON_PIN),
                                 .mode = GPIO_MODE_INPUT,
                                 .pull_up_en = GPIO_PULLUP_ENABLE,
                                 .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                 .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&button_config);

  // Check if button is pressed (active low with pull-up)
  button_boot = (gpio_get_level(CONFIG_BUTTON_PIN) == 0);

  if (button_boot) {
    ESP_LOGI(TAG, "Boot button pressed - forcing configuration mode");
  } else {
    ESP_LOGI(TAG, "Boot button not pressed");
  }
#else
  ESP_LOGI(TAG, "No button pin defined - skipping button check");
#endif

  ESP_LOGI(TAG, "Check for button press");

  // Setup the device flash storage.
  if (flash_initialize()) {
    ESP_LOGE(TAG, "failed to initialize flash");
    return;
  }
  ESP_LOGI(TAG, "finished flash init");
  esp_register_shutdown_handler(&flash_shutdown);

  // Initialize NVS settings
  ESP_ERROR_CHECK(nvs_settings_init());

  // Setup WiFi.
  ESP_LOGI(TAG, "Initializing WiFi manager...");
  // Pass empty strings to force AP mode
  if (wifi_initialize("", "")) {
    ESP_LOGE(TAG, "failed to initialize WiFi");
    return;
  }
  esp_register_shutdown_handler(&wifi_shutdown);

  image_url = nvs_get_image_url();

  // Setup the display.
  if (gfx_initialize(image_url)) {
    ESP_LOGE(TAG, "failed to initialize gfx");
    return;
  }
  esp_register_shutdown_handler(&display_shutdown);

  // Start the AP web server now that display memory is allocated
  if (nvs_get_ap_mode()) {
    ESP_LOGI(TAG, "Starting AP Web Server...");
    ap_start();
  }

  // Register callback to detect configuration events
  wifi_register_config_callback(config_saved_callback);

  // Wait a bit for the AP to start
  vTaskDelay(pdMS_TO_TICKS(1000));

  uint8_t mac[6];
  if (!wifi_get_mac(mac)) {
    ESP_LOGI(TAG, "WiFi MAC: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
  }

  // Wait for WiFi connection (with a 60-second timeout)
  // This will block until either connected or timeout or short circuit if
  // button was held during button.
  bool sta_connected = wifi_wait_for_connection(60000);

  if (sta_connected) {
    ESP_LOGI(TAG, "WiFi connected successfully!");

    if (nvs_get_prefer_ipv6()) {
      ESP_LOGI(TAG, "IPv6 preference enabled, waiting for global address...");
      // Prefer IPv6: Wait a short time for IPv6 address to be acquired
      if (wifi_wait_for_ipv6(5000)) {
        ESP_LOGI(TAG, "IPv6 Ready!");
      } else {
        ESP_LOGI(TAG,
                 "IPv6 not available or timed out, proceeding with existing "
                 "connection (IPv4)");
      }
    }

    // Start SNTP now that network is up
    if (!esp_sntp_enabled()) {
      esp_sntp_init();
    }

    // Initialize Syslog
    char syslog_addr[MAX_SYSLOG_ADDR_LEN + 1];
    if (nvs_get_syslog_addr(syslog_addr, sizeof(syslog_addr)) == ESP_OK &&
        strlen(syslog_addr) > 0) {
      syslog_init(syslog_addr);
    }
  }

  if (nvs_get_ap_mode()) {
    if (button_boot || !sta_connected) {
      ESP_LOGW(TAG, "WiFi didn't connect or Boot Button Pressed");
      // Load up the config webp so that we don't just loop the boot screen over
      // and over again but show the ap config info webp
      ESP_LOGI(TAG, "Loading Config WEBP");

      if (gfx_display_asset("config")) {
        ESP_LOGE(TAG,
                 "Failed to display config screen - continuing without it");
        // Don't crash, just continue - the AP is still running
      }
    }
  } else {
    if (!sta_connected) {
      ESP_LOGW(TAG,
               "WiFi didn't connect and AP mode is disabled - check secrets");
    } else {
      if (button_boot) {
        ESP_LOGW(TAG,
                 "Boot button pressed but AP mode disabled; skipping "
                 "configuration portal");
      }
    }
  }

  // Wait for configuration when boot button was pressed
  if (button_boot) {
    if (!nvs_get_ap_mode()) {
      ESP_LOGW(TAG,
               "Boot button pressed but AP mode disabled; skipping "
               "configuration wait");
    } else {
      ESP_LOGW(TAG,
               "Boot button pressed - waiting for configuration or timeout...");
      int config_wait_counter = 0;
      while (config_wait_counter < 120) {  // Wait up xx seconds
        // Check if configuration was received
        if (config_received) {
          ESP_LOGI(TAG, "Configuration received - proceeding");
          break;
        }

        config_wait_counter++;
        vTaskDelay(pdMS_TO_TICKS(1 * 1000));
      }
    }
  } else if (!wifi_is_connected()) {
    ESP_LOGW(TAG, "Pausing main task until wifi connected...");
    while (!wifi_is_connected()) {
      static int counter = 0;
      counter++;
      vTaskDelay(pdMS_TO_TICKS(1 * 1000));
      if (counter > 600)
        esp_restart();  // after 10 minutes reboot because maybe we got stuck
                        // here after power outage or something.
    }
  }

  // When AP mode is enabled, auto-shutdown the AP after a short delay
  if (nvs_get_ap_mode()) {
    ap_start_shutdown_timer();
  }

  while (true) {
    image_url = nvs_get_image_url();

    if (image_url != NULL && strlen(image_url) > 0) {
      // It's not blank now
      break;
    }

    ESP_LOGW(TAG, "Image URL is not set. Waiting for configuration...");
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  // image_url is now valid and usable here
  ESP_LOGI(TAG, "Proceeding with image URL: %s", image_url);

  ESP_LOGI(TAG, "Free heap: %" PRIu32, esp_get_free_heap_size());
  ESP_LOGI(TAG, "Free PSRAM: %" PRIu32,
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  ESP_LOGI(TAG, "Free internal RAM: %" PRIu32,
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  // Check for ws:// or wss:// in the URL
  if (strncmp(image_url, "ws://", 5) == 0 ||
      strncmp(image_url, "wss://", 6) == 0) {
    ESP_LOGI(TAG, "Using websockets with URL: %s", image_url);
    use_websocket = true;
    // setup ws event handlers
    const esp_websocket_client_config_t ws_cfg = {
        .uri = image_url,
        .task_stack = 8192,
        .buffer_size = 8192,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .reconnect_timeout_ms = 10000,
        .network_timeout_ms = 10000,
    };
    ws_handle = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(ws_handle, WEBSOCKET_EVENT_ANY,
                                  websocket_event_handler, (void*)ws_handle);

    // Set the websocket handle in gfx module for bidirectional communication
    gfx_set_websocket_handle(ws_handle);

    // Create event group for WebSocket connection
    s_ws_event_group = xEventGroupCreate();

    // Start the client immediately
    esp_err_t err = esp_websocket_client_start(ws_handle);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to start WebSocket client: %d", err);
    }

    // Wait for connection with a timeout (e.g., 5 seconds)
    // If it fails to connect in time, the loop below will handle reconnection
    // logic
    xEventGroupWaitBits(s_ws_event_group, WS_CONNECTED_BIT, pdFALSE, pdTRUE,
                        pdMS_TO_TICKS(5000));

    bool was_connected = false;
    for (;;) {
      bool is_connected = esp_websocket_client_is_connected(ws_handle);

      if (is_connected) {
        if (!was_connected) {
          ESP_LOGI(TAG, "WebSocket connected, sending client info");
          send_client_info();
          was_connected = true;
        }
      } else {
        if (was_connected) {
          was_connected = false;
          ESP_LOGW(TAG, "WebSocket Disconnected");
        }
        ESP_LOGW(TAG, "WebSocket not connected. Attempting to reconnect...");
        esp_websocket_client_stop(ws_handle);
        esp_err_t err = esp_websocket_client_start(ws_handle);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "Reconnection failed with error %d", err);
        }
      }

      wifi_health_check();
      vTaskDelay(pdMS_TO_TICKS(5000));  // check every 5s
    }
  } else {
    // normal http
    ESP_LOGW(TAG, "HTTP Loop Start with URL: %s", image_url);
    for (;;) {
      uint8_t* webp;
      size_t len;
      static uint8_t brightness_pct = DISPLAY_DEFAULT_BRIGHTNESS;
      int status_code = 0;
      ESP_LOGI(TAG, "Fetching from URL: %s", image_url);
      char* ota_url = NULL;

      // Start timing the HTTP fetch
      int64_t fetch_start_us = esp_timer_get_time();
      bool fetch_failed = !wifi_is_connected() ||
                          remote_get(image_url, &webp, &len, &brightness_pct,
                                     &app_dwell_secs, &status_code, &ota_url);
      int64_t fetch_duration_ms =
          (esp_timer_get_time() - fetch_start_us) / 1000;

      ESP_LOGI(TAG, "HTTP fetch returned in %lld ms", fetch_duration_ms);

      if (ota_url != NULL) {
        ESP_LOGI(TAG, "OTA URL received via HTTP: %s", ota_url);
        xTaskCreate(ota_task_entry, "ota_task", 8192, ota_url, 5, NULL);
        // Since we are rebooting (if successful) or failed, we might want to
        // continue or pause. If OTA failed, we continue normal operation.
      }

      if (fetch_failed) {
        ESP_LOGE(TAG, "No WiFi or Failed to get webp with code %d",
                 status_code);
        vTaskDelay(pdMS_TO_TICKS(1 * 1000));
        draw_error_indicator_pixel();  // Add this
        if (status_code == 0) {
          ESP_LOGI(TAG, "No connection");
        } else if (status_code == 404 || status_code == 400) {
          ESP_LOGI(TAG, "HTTP 404/400, displaying 404");
          if (gfx_display_asset("error_404")) {
            ESP_LOGE(TAG, "Failed to display 404 screen");
          }
          vTaskDelay(pdMS_TO_TICKS(1 * 5000));
        } else if (status_code == 413) {
          ESP_LOGI(TAG,
                   "Content too large - oversize graphic already displayed");
          vTaskDelay(pdMS_TO_TICKS(1 * 5000));
        }
      } else {
        // Successful remote_get
        display_set_brightness(brightness_pct);
        ESP_LOGI(TAG, "Queuing new webp (%d bytes)", len);

        int queued_counter = gfx_update(webp, len, app_dwell_secs);
        // Do not free(webp) here; ownership is transferred to gfx
        webp = NULL;

        // Wait for the current animation to finish (isAnimating will be 0)
        if (isAnimating > 0) {
          ESP_LOGI(TAG, "Waiting for current webp to finish");
          while (isAnimating > 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
          }
        }

        // Wait for gfx task to load the newly queued image before fetching the
        // next one This ensures the image has begun displaying before we fetch
        // again
        // ESP_LOGI(TAG, "Waiting for gfx task to load new image (counter=%d)", queued_counter);
        int timeout = 0;
        while (gfx_get_loaded_counter() != queued_counter && timeout < 20000) {
          vTaskDelay(pdMS_TO_TICKS(10));
          timeout += 10;
        }
        if (timeout >= 20000) {
          ESP_LOGE(TAG, "Timeout waiting for gfx task to load image");
        } else {
          ESP_LOGI(TAG, "Gfx task loaded image counter %d ms", queued_counter);
        }

        // ESP_LOGD(TAG, "Setting isAnimating to 1");
        isAnimating = 1;
      }
      wifi_health_check();
    }
  }
}
