#include <assets.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <webp/demux.h>
#include <esp_websocket_client.h>

#include "display.h"
#include "flash.h"
#include "gfx.h"
#include "remote.h"
#include "sdkconfig.h"
#include "wifi.h"

#define BLUE "\033[1;34m"
#define RESET "\033[0m"  // Reset to default color

static const char* TAG = "main";
int32_t isAnimating =
    5;  // Initialize with a valid value enough time for boot animation
int32_t app_dwell_secs = REFRESH_INTERVAL_SECONDS;
bool use_websocket = false;
esp_websocket_client_handle_t ws_handle;

static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data) {
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
      break;
    case WEBSOCKET_EVENT_DATA:
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
      ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
      ESP_LOGW(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);
      ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n",
        data->payload_len, data->data_len, data->payload_offset);
      // Check if this is a complete message or just a fragment
      bool is_complete =
          (data->payload_offset + data->data_len >= data->payload_len);

      if (is_complete) {
        ESP_LOGI(TAG, "Message is complete");
      } else {
        ESP_LOGI(TAG, "Message is fragmented - received %d/%d bytes",
                 data->payload_offset + data->data_len, data->payload_len);
      }

      // Check if data contains "brightness"
      if (strstr((char *)data->data_ptr, "{\"brightness\":")) {
        ESP_LOGI(TAG, "Brightness data detected");
        
        // Simple string parsing for {"brightness": xxx}
        char *brightness_pos = strstr((char *)data->data_ptr, "brightness");
        if (brightness_pos) {
          // Find position after the space that follows the colon
          char *value_start = brightness_pos + 13; // brightness": is 13 chars
          
          // Parse the integer value directly
          int brightness_value = atoi(value_start);
          ESP_LOGI(TAG, "Parsed brightness: %d", brightness_value);
          
          // Clamp value between min and max
          if (brightness_value < DISPLAY_MIN_BRIGHTNESS) brightness_value = DISPLAY_MIN_BRIGHTNESS;
          if (brightness_value > DISPLAY_MAX_BRIGHTNESS) brightness_value = DISPLAY_MAX_BRIGHTNESS;
          
          // Set the brightness
          display_set_brightness((uint8_t)brightness_value);
        }
      } else if (data->data_ptr[0] != '{') {
        // Binary data (WebP image)
        ESP_LOGI(TAG, "Binary data detected (WebP image)");
        
        // Process the binary data as a WebP image
        gfx_update((uint8_t *)data->data_ptr, data->data_len);
        
        // We don't control timing during websocket operation so just set this to 1.
        isAnimating = 1;
      }

      break;
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
      break;
  }
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

  // Setup WiFi.
  if (wifi_initialize(WIFI_SSID, WIFI_PASSWORD)) {
    ESP_LOGE(TAG, "failed to initialize WiFi");
    return;
  }
  esp_register_shutdown_handler(&wifi_shutdown);

  uint8_t mac[6];
  if (!wifi_get_mac(mac)) {
    ESP_LOGI(TAG, "WiFi MAC: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
  }

  // Check for ws:// or wss:// in REMOTE_URL
  if (strstr(REMOTE_URL, "ws://") != NULL) {
    ESP_LOGI(TAG,"Using websockets");
    use_websocket = true;
    // setup ws event handlers
    const esp_websocket_client_config_t ws_cfg = {
      .uri = REMOTE_URL,
      .buffer_size = HTTP_BUFFER_SIZE_MAX};
    ws_handle = esp_websocket_client_init(&ws_cfg);
    esp_err_t start_error = esp_websocket_client_start(ws_handle);
    if (start_error != ESP_OK) {
      ESP_LOGE(TAG, "couldn't connect to websocket url %s with error code %i", REMOTE_URL, start_error);
      // display error ?
    } else {
      // esp_websocket_register_events(ws_handle, RX_EVENT, RX_HANDLER_FUNC,
      //                               void *event_handler_arg)
      esp_websocket_register_events(ws_handle, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)ws_handle);
      esp_websocket_client_start(ws_handle);
    }
  }
  else 
  {
    // normal http
    ESP_LOGW(TAG, "HTTP Loop Start");
    for (;;) {
      uint8_t* webp;
      size_t len;
      static uint8_t brightness_pct = DISPLAY_DEFAULT_BRIGHTNESS;

      if ( use_websocket ) {
        // let the events do the work.

      } else {

        if (remote_get(REMOTE_URL, &webp, &len, &brightness_pct,
                      &app_dwell_secs)) {
          ESP_LOGE(TAG, "Failed to get webp");
          vTaskDelay(pdMS_TO_TICKS(1 * 5000));
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
          ESP_LOGI(TAG, BLUE "Setting isAnimating to %d" RESET,
                  (int)app_dwell_secs);
          isAnimating = app_dwell_secs;  // use isAnimating as the container for
                                        // app_dwell_secs
          vTaskDelay(pdMS_TO_TICKS(1000));
        }
      }
    }
  }
}
