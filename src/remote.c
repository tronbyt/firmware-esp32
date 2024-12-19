#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_tls.h>
#include <stdlib.h>

static const char* TAG = "remote";

struct remote_state {
  void* buf;
  size_t len;
  size_t size;
  size_t max;
};

int brightness_value = -1;
int dwell_secs_value = -1;

#define HTTP_BUFFER_SIZE_MAX 512 * 1024
#define HTTP_BUFFER_SIZE_DEFAULT 32 * 1024

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

    static esp_err_t
    _httpCallback(esp_http_client_event_t* event) {
  esp_err_t err = ESP_OK;

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
      // Check for the specific header key
      if (strcmp(event->header_key, "Tronbyt-Brightness") == 0) {
        brightness_value = (int)atoi(event->header_value);
        ESP_LOGI(TAG, "Tronbyt-Brightness value: %i", brightness_value);
      }
      else if (strcmp(event->header_key, "Tronbyt-Dwell-Secs") == 0) {
        dwell_secs_value = (int)atoi(event->header_value);
        ESP_LOGI(TAG, "Tronbyt-Dwell-Secs value: %i", dwell_secs_value);
      }
      break;

    case HTTP_EVENT_ON_DATA:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", event->data_len);

      if (event->user_data == NULL) {
        ESP_LOGW(TAG, "Discarding HTTP response due to missing state");
        break;
      }

      struct remote_state* state = (struct remote_state*)event->user_data;

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
  }

  return err;
}

int remote_get(const char* url, uint8_t** buf, size_t* len, int* b_int, int* dwell_secs) {
  // State for processing the response
  struct remote_state state = {
      .buf = malloc(HTTP_BUFFER_SIZE_DEFAULT),
      .len = 0,
      .size = HTTP_BUFFER_SIZE_DEFAULT,
      .max = HTTP_BUFFER_SIZE_MAX,
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
      .timeout_ms = 10e3,
      .crt_bundle_attach = esp_crt_bundle_attach,
  };

  esp_http_client_handle_t http = esp_http_client_init(&config);

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

  // Write back the results.
  *buf = state.buf;
  *len = state.len;
  if (brightness_value > -1 && brightness_value < 255) *b_int = brightness_value;
  if (dwell_secs_value > -1 && dwell_secs_value < 300) *dwell_secs = dwell_secs_value; // 5 minute max ?

  esp_http_client_cleanup(http);

  return 0;
}
