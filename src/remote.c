#include <stdbool.h>
#include <stdlib.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_tls.h>
#include "gfx.h"

static const char* TAG = "remote";

struct remote_state {
  void* buf;
  size_t len;
  size_t size;
  size_t max;
  uint8_t brightness;
  int32_t dwell_secs;
  bool oversize_detected;
};

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static esp_err_t _httpCallback(esp_http_client_event_t* event) {
  esp_err_t err = ESP_OK;
  struct remote_state* state = (struct remote_state*)event->user_data;

  switch (event->event_id) {
    case HTTP_EVENT_ERROR:
      ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
      break;

    case HTTP_EVENT_ON_CONNECTED:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
      break;

    case HTTP_EVENT_HEADER_SENT:
      ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
      break;

    case HTTP_EVENT_ON_HEADER:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", event->header_key,
               event->header_value);
      
      // Check for the Content-Length header
      if (strcasecmp(event->header_key, "Content-Length") == 0) {
        size_t content_length = (size_t)atoi(event->header_value);
        if (content_length > state->max) {
          ESP_LOGE(TAG,
                   "Content-Length (%d bytes) exceeds allowed max (%d bytes)",
                   content_length, state->max);
          // Display the oversize graphic
          if (gfx_display_asset("oversize") != 0) {
            ESP_LOGE(TAG, "Failed to display oversize graphic");
          }
          state->oversize_detected = true;
          err = ESP_ERR_NO_MEM;
          esp_http_client_close(event->client);  // Abort the HTTP request
        } else {
          ESP_LOGI(TAG, "Content-Length Header : %d", content_length);
        }
      }

      // Check for the specific header key
      if (strcasecmp(event->header_key, "Tronbyt-Brightness") == 0) {
        state->brightness = (uint8_t)atoi(event->header_value); // API spec: 0-100
        ESP_LOGD(TAG, "Tronbyt-Brightness value: %d%%", state->brightness);
      }
      else if (strcasecmp(event->header_key, "Tronbyt-Dwell-Secs") == 0) {
        state->dwell_secs = (int)atoi(event->header_value);
        // ESP_LOGI(TAG, "Tronbyt-Dwell-Secs value: %i", dwell_secs_value);
      }
      break;

    case HTTP_EVENT_ON_DATA:

      if (event->user_data == NULL) {
        ESP_LOGW(TAG, "Discarding HTTP response due to missing state");
        break;
      }

      // If oversize was detected, don't process any data
      if (state->oversize_detected) {
        ESP_LOGD(TAG, "Discarding HTTP data due to oversize detection");
        break;
      }

      // if (event->data_len > max_data_size) {
      //   ESP_LOGW(TAG, "Discarding HTTP response due to missing state");
      //   break;
      // }

      // If needed, resize the buffer to fit the new data
      if (event->data_len + state->len > state->size) {
        // Determine new size
        state->size =
            MAX(MIN(state->size * 2, state->max), state->len + event->data_len);
        if (state->size > state->max) {
          ESP_LOGE(TAG, "Response size exceeds allowed max (%d bytes)",
                   state->max);
          free(state->buf);
          err = ESP_ERR_NO_MEM;
          break;
        }

        // And reallocate
        void* new = realloc(state->buf, state->size);
        if (new == NULL) {
          ESP_LOGE(TAG, "Resizing response buffer failed");
          free(state->buf);
          err = ESP_ERR_NO_MEM;
          break;
        }
        state->buf = new;
      }

      // Copy over the new data
      memcpy(state->buf + state->len, event->data, event->data_len);
      state->len += event->data_len;
      break;

    case HTTP_EVENT_ON_FINISH:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
      break;

    case HTTP_EVENT_DISCONNECTED:
      ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");

      int mbedtlsErr = 0;
      esp_err_t err =
          esp_tls_get_and_clear_last_error(event->data, &mbedtlsErr, NULL);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP error - %s (mbedtls: 0x%x)", esp_err_to_name(err),
                 mbedtlsErr);
      }
      break;

    case HTTP_EVENT_REDIRECT:
      ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
      esp_http_client_set_redirection(event->client);
      break;
  }

  return err;
}

int remote_get(const char* url, uint8_t** buf, size_t* len, uint8_t* brightness_pct, int32_t* dwell_secs, int* return_status_code) {
  // State for processing the response
  struct remote_state state = {
      .buf = malloc(HTTP_BUFFER_SIZE_DEFAULT),
      .len = 0,
      .size = HTTP_BUFFER_SIZE_DEFAULT,
      .max = HTTP_BUFFER_SIZE_MAX,
      .brightness = -1,
      .dwell_secs = -1,
      .oversize_detected = false,
  };

  if (state.buf == NULL) {
    ESP_LOGE(TAG, "couldn't allocate HTTP receive buffer");
    return 1;
  }

  // Set up http client
  esp_http_client_config_t config = {
      .url = url,
      .event_handler = _httpCallback,
      .user_data = &state,
      .timeout_ms = 20e3,  // Increased from 10s to 20s
      .crt_bundle_attach = esp_crt_bundle_attach,
  };

  esp_http_client_handle_t http = esp_http_client_init(&config);
  if (http == NULL) {
    ESP_LOGE(TAG, "HTTP client initialization failed for URL: %s", url);
    free(state.buf);
    return 1;
  }

  // Do the request
  esp_err_t err = esp_http_client_perform(http);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "couldn't reach %s: %s", url, esp_err_to_name(err));
    if (state.buf != NULL) {
      free(state.buf);
    }
    esp_http_client_cleanup(http);
    return 1;
  }

  // Check if oversize was detected during the request
  if (state.oversize_detected) {
    ESP_LOGI(TAG, "Request aborted due to oversize content");
    if (state.buf != NULL) {
      free(state.buf);
    }
    esp_http_client_cleanup(http);
    *return_status_code = 413;  // HTTP 413 Payload Too Large
    return 1;  // Return error so main loop doesn't process the result
  }

  int status_code = esp_http_client_get_status_code(http);
  *return_status_code = status_code;
  if (status_code != 200) {
    ESP_LOGE(TAG, "Server returned HTTP status %d", status_code);
    if (state.buf != NULL) {
      free(state.buf);
    }
    esp_http_client_cleanup(http);
    return 1;
  }

  // Write back the results.
  *buf = state.buf;
  *len = state.len;
  *brightness_pct = state.brightness; // Assumes API provides 0–100 as spec'd
  if (state.dwell_secs > -1 && state.dwell_secs < 300) *dwell_secs = state.dwell_secs; // 5 minute max ?

  esp_http_client_cleanup(http);
  // ESP_LOGI(TAG,"fetched new webp");
  return 0;
}
