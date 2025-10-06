#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <webp/demux.h>
#include <esp_websocket_client.h>
#include <esp_crt_bundle.h>
#include <esp_timer.h>
#include <ctype.h> // For isdigit

#include "display.h"
#include "flash.h"
#include "gfx.h"
#include "remote.h"
#include "sdkconfig.h"
#include "wifi.h"

#ifdef BUTTON_PIN
#include <driver/gpio.h>
#endif

#define BLUE "\033[1;34m"
#define RESET "\033[0m"  // Reset to default color

// Default URL if none is provided through WiFi manager
#define DEFAULT_URL "http://URL.NOT.SET/"

static const char* TAG = "main";
int32_t isAnimating = 1;
int32_t app_dwell_secs = REFRESH_INTERVAL_SECONDS;
uint8_t *webp; // main buffer downloaded webp data
static bool websocket_oversize_detected = false; // Flag to track oversize websocket messages

bool use_websocket = false;
esp_websocket_client_handle_t ws_handle;

bool button_boot = false;

bool config_received = false;



void config_saved_callback(void) {
  config_received = true;
  ESP_LOGI(TAG, "Configuration saved - signaling main task");
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data) {
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
      draw_error_indicator_pixel();
      break;
    case WEBSOCKET_EVENT_DATA:
      // ESP_LOGI(TAG, "---------------------WEBSOCKET_EVENT_DATA");
      // ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
      // ESP_LOGW(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);
      // ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n",
      //  data->payload_len, data->data_len, data->payload_offset);
      // Check if this is a complete message or just a fragment
      bool is_complete =
          (data->payload_offset + data->data_len >= data->payload_len);

      if (is_complete) {
        // ESP_LOGI(TAG, "Message is complete");

      } else {
        // ESP_LOGI(TAG, "Message is fragmented - received %d/%d bytes",
        //          data->payload_offset + data->data_len, data->payload_len);
      }

      // Check if data contains "brightness"
      if (data->op_code == 1 && strstr((char *)data->data_ptr, "{\"brightness\":")) {
        ESP_LOGI(TAG, "Brightness data detected");

        // Simple string parsing for {"brightness": xxx}
        char *brightness_pos = strstr((char *)data->data_ptr, "brightness");
        if (brightness_pos) {
          // Find position after the space that follows the colon
          char *value_str_start = brightness_pos + 13; // "brightness\":" is 13 chars long

          // Safely parse the integer value
          char temp_val_buf[12]; // Buffer for up to 10 digits (e.g., "2147483647") + sign + null
          int k = 0;
          // Iterate while char is digit and we have space in temp_val_buf (excluding null term)
          // and we are within data->data_ptr + data->data_len bounds
          while (k < (sizeof(temp_val_buf) - 1) && (value_str_start + k < (char*)data->data_ptr + data->data_len) && isdigit((unsigned char)value_str_start[k])) {
              temp_val_buf[k] = value_str_start[k];
              k++;
          }
          temp_val_buf[k] = '\0'; // Null-terminate the copied digits

          int brightness_value = 0;
          if (k > 0) { // Only call atoi if we copied some digits
            brightness_value = atoi(temp_val_buf);
            ESP_LOGI(TAG, "Parsed brightness: %d from string '%s'", brightness_value, temp_val_buf);
          } else {
            ESP_LOGW(TAG, "No digits found for brightness value. Original string segment starts with: %.5s", value_str_start);
            // Default to a safe brightness or handle as an error? For now, 0.
            brightness_value = -1; // Indicate parsing failure or use a default
          }

          if (brightness_value == -1) { // If parsing failed
            // Optionally, log error and skip setting brightness or set a default
            ESP_LOGE(TAG, "Failed to parse brightness value from WebSocket message.");
            // Keep existing brightness or set to a safe default like DISPLAY_MIN_BRIGHTNESS
            // For now, we'll just not update if parsing fails.
          } else {
            // Clamp value between min and max
            if (brightness_value < DISPLAY_MIN_BRIGHTNESS) brightness_value = DISPLAY_MIN_BRIGHTNESS;
            if (brightness_value > DISPLAY_MAX_BRIGHTNESS) brightness_value = DISPLAY_MAX_BRIGHTNESS;

            // Set the brightness
            display_set_brightness((uint8_t)brightness_value);
          }
        }
      } else if (data->op_code == 2) {
        // Binary data (WebP image)
        // ESP_LOGI(TAG, "Binary data detected (WebP image)");

        // Check if this is a complete message or just a fragment
        bool is_complete =
            (data->payload_offset + data->data_len >= data->payload_len);

        if (is_complete) {
          // ESP_LOGI(TAG, "Message is complete");
          // Reset oversize flag for next message
          websocket_oversize_detected = false;
        } else {
          // ESP_LOGI(TAG, "Message is fragmented - received %d/%d bytes",
          //          data->payload_offset + data->data_len, data->payload_len);
        }

        // Check if payload size exceeds maximum buffer size (only on first fragment)
        if (data->payload_offset == 0 && data->payload_len > HTTP_BUFFER_SIZE_MAX) {
          ESP_LOGE(TAG, "WebP payload size (%d bytes) exceeds maximum buffer size (%d bytes)",
                   data->payload_len, HTTP_BUFFER_SIZE_MAX);
          // Display the oversize graphic
          if (gfx_display_asset("oversize") != 0) {
            ESP_LOGE(TAG, "Failed to display oversize graphic");
          }
          websocket_oversize_detected = true;
          break;
        }

        // Skip processing if oversize was detected for this message
        if (websocket_oversize_detected) {
          ESP_LOGD(TAG, "Skipping fragment due to oversize detection");
          break;
        }

        // First fragment or complete message - allocate memory
        if (data->payload_offset == 0) {
          // Free previous buffer if it exists
          if (webp != NULL) {
            free(webp);
            webp = NULL;
          }

          // Allocate memory for the full payload
          webp = malloc(data->payload_len);
          if (webp == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for WebP image");
            break;
          }
        }

        // Ensure we have a valid buffer
        if (webp == NULL) {
          ESP_LOGE(TAG, "WebP buffer is NULL, skipping fragment");
          break;
        }

        // Copy this fragment to the appropriate position in the buffer
        memcpy(webp + data->payload_offset, data->data_ptr, data->data_len);

        // If complete, process the WebP image
        if (is_complete) {
          // Process the complete binary data as a WebP image
          gfx_update(webp, data->payload_len, app_dwell_secs);

          // We don't control timing during websocket so use this to notify new data and to break out of current animation.
          isAnimating = -1;

          // Do not free(webp) here; ownership is transferred to gfx
          webp = NULL;
        }
      }

      break;
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
      draw_error_indicator_pixel();
      break;
  }
}

void app_main(void) {
  // delete here for 5 seconds to allow for serial port to connect.
  ESP_LOGI(TAG, "App Main Start");

#ifdef BUTTON_PIN
  // Configure button pin as input with pull-up
  gpio_config_t button_config = {
    .pin_bit_mask = (1ULL << BUTTON_PIN),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&button_config);

  // Check if button is pressed (active low with pull-up)
  button_boot = (gpio_get_level(BUTTON_PIN) == 0);
  
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
  ESP_LOGI(TAG,"finished flash init");
  esp_register_shutdown_handler(&flash_shutdown);

  // Setup the display.
  if (gfx_initialize()) {
    ESP_LOGE(TAG, "failed to initialize gfx");
    return;
  }
  esp_register_shutdown_handler(&display_shutdown);

  // Setup WiFi.
  ESP_LOGI(TAG, "Initializing WiFi manager...");
  // Pass empty strings to force AP mode
  if (wifi_initialize("", "")) {
    ESP_LOGE(TAG, "failed to initialize WiFi");
    return;
  }
  esp_register_shutdown_handler(&wifi_shutdown);

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
  // This will block until either connected or timeout or short circuit if button was held during button.
  if (button_boot || !wifi_wait_for_connection(60000)) {

    ESP_LOGW(TAG, "WiFi didn't connect or Boot Button Pressed");
    // Load up the config webp so that we don't just loop the boot screen over
    // and over again but show the ap config info webp
    ESP_LOGI(TAG, "Loading Config WEBP");

    if (gfx_display_asset("config")) {
      ESP_LOGE(TAG, "Failed to display config screen - continuing without it");
      // Don't crash, just continue - the AP is still running
    }
  } else {
    ESP_LOGI(TAG, "WiFi connected successfully!");
  }

  // Wait for configuration when boot button was pressed
  if (button_boot) {
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
  } else if(!wifi_is_connected()) {
    ESP_LOGW(TAG,"Pausing main task until wifi connected . . . ");
    while (!wifi_is_connected()) {
      static int counter = 0;
      counter++;
      vTaskDelay(pdMS_TO_TICKS(1 * 1000));
      if (counter > 600) esp_restart(); // after 10 minutes reboot because maybe we got stuck here after power outage or something.
    }
  }


  // Create a timer to auto-shutdown the AP after 5 minutes
  TimerHandle_t ap_shutdown_timer = xTimerCreate(
      "ap_shutdown_timer",
      pdMS_TO_TICKS(2 * 60 * 1000),  // 2 minutes in milliseconds
      pdFALSE,                       // One-shot timer
      NULL,                          // No timer ID
      wifi_shutdown_ap               // Callback function (now with correct signature)
  );

  if (ap_shutdown_timer != NULL) {
    // Timer created successfully, now try to start it
    BaseType_t timer_started = xTimerStart(ap_shutdown_timer, 0);
    if (timer_started == pdPASS) {
      ESP_LOGI(TAG, "AP will automatically shut down in 2 minutes");
    } else {
      ESP_LOGE(TAG, "Failed to start AP shutdown timer");
      // Clean up the timer if we couldn't start it
      xTimerDelete(ap_shutdown_timer, 0);
    }
  } else {
    ESP_LOGE(TAG, "Failed to create AP shutdown timer");
  }

  const char *image_url = NULL;

  while (true) {
    image_url = wifi_get_image_url();

    if (image_url != NULL && strlen(image_url) > 0 ) {
      // It's not blank now
      break;
    }

    ESP_LOGW(TAG, "Image URL is not set. Waiting for configuration...");
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  // image_url is now valid and usable here
  ESP_LOGI(TAG, "Proceeding with image URL: %s", image_url);

  // Check for ws:// or wss:// in the URL
  if (strncmp(image_url, "ws://", 5) == 0 || strncmp(image_url, "wss://", 6) == 0) {
    ESP_LOGI(TAG, "Using websockets with URL: %s", image_url);
    use_websocket = true;
    // setup ws event handlers
    const esp_websocket_client_config_t ws_cfg = {
      .uri = image_url,
      .task_stack  = 8192,
      .buffer_size = 10000,
      .crt_bundle_attach = esp_crt_bundle_attach,
    };
    ws_handle = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(ws_handle, WEBSOCKET_EVENT_ANY,
                                  websocket_event_handler,
                                  (void *)ws_handle);
    for (;;) {
      if (!esp_websocket_client_is_connected(ws_handle)) {
        ESP_LOGW(TAG, "WebSocket not connected. Attempting to reconnect...");
        esp_websocket_client_stop(ws_handle);  // Safe even if already stopped
        esp_err_t err = esp_websocket_client_start(ws_handle);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "Reconnection failed with error %d", err);
        } else {
          ESP_LOGI(TAG, "Reconnected to WebSocket server.");
        }
      }
      wifi_health_check();
      // Do other stuff or vTaskDelay
      vTaskDelay(pdMS_TO_TICKS(5000));  // check every 5s
    }
  } else {
    // normal http
    ESP_LOGW(TAG, "HTTP Loop Start with URL: %s", image_url);
    for (;;) {
      uint8_t *webp;
      size_t len;
      static uint8_t brightness_pct = DISPLAY_DEFAULT_BRIGHTNESS;
      int status_code = 0;
      ESP_LOGI(TAG, "Fetching from URL: %s", image_url);

      // Start timing the HTTP fetch
      int64_t fetch_start_us = esp_timer_get_time();
      bool fetch_failed = !wifi_is_connected() || remote_get(image_url, &webp, &len,
                                         &brightness_pct, &app_dwell_secs, &status_code);
      int64_t fetch_duration_ms = (esp_timer_get_time() - fetch_start_us) / 1000;

      ESP_LOGI(TAG, "HTTP fetch returned in %lld ms", fetch_duration_ms);

      if (fetch_failed) {
        ESP_LOGE(TAG, "No WiFi or Failed to get webp with code %d",status_code);
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
          ESP_LOGI(TAG, "Content too large - oversize graphic already displayed");
          vTaskDelay(pdMS_TO_TICKS(1 * 5000));
        }
      } else {
        // Successful remote_get
        display_set_brightness(brightness_pct);
        ESP_LOGI(TAG, BLUE "Queuing new webp (%d bytes)" RESET, len);

        int queued_counter = gfx_update(webp, len, app_dwell_secs);
        // Do not free(webp) here; ownership is transferred to gfx
        webp = NULL;
        // Wait for app_dwell_secs to expire (isAnimating will be 0)
        // ESP_LOGI(TAG, BLUE "isAnimating is %d" RESET, (int)isAnimating);
        bool long_fetch = false;
        if (isAnimating > 0) {
          ESP_LOGI(TAG, BLUE "Waiting for current webp to finish" RESET);
        } else {
          ESP_LOGE(TAG,"long fetch took place");
          long_fetch = true;
        }
        while (isAnimating > 0) {
          // ESP_LOGI(TAG, BLUE "Delay 1" RESET);
          vTaskDelay(pdMS_TO_TICKS(1));
        }

        // If long fetch occurred, wait for gfx task to load the image BEFORE setting isAnimating
        // Skip wait for first couple images (counter 0 or 1) as they load immediately
        if (long_fetch && queued_counter > 1) {
          ESP_LOGI(TAG, "Waiting for gfx task to load new image after long fetch (counter=%d)", queued_counter);
          int timeout = 0;
          while (gfx_get_loaded_counter() != queued_counter && timeout < 20000) {
            vTaskDelay(pdMS_TO_TICKS(10));
            timeout += 10;
          }
          if (timeout >= 20000) {
            ESP_LOGE(TAG, "Timeout waiting for gfx task to load image");
          } else {
            ESP_LOGI(TAG, "Gfx task loaded image after %d ms", timeout);
          }
        } else if (long_fetch) {
          ESP_LOGI(TAG, "Skipping wait for early image (counter=%d)", queued_counter);
        }

        ESP_LOGI(TAG, BLUE "Setting isAnimating to 1" RESET);
        isAnimating = 1;
      }
    wifi_health_check();
    }
  }

}
