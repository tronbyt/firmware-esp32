#include "remote.h"

#include <cstdlib>
#include <cstring>

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_tls.h>

#include "sdkconfig.h"
#include "version.h"
#include "webp_player.h"

namespace {

const char* TAG = "remote";

struct RemoteState {
  void* buf;
  size_t len;
  size_t size;
  size_t max;
  uint8_t brightness;
  int32_t dwell_secs;
  char* ota_url;
  bool oversize_detected;
};

template <typename T>
constexpr T max_val(T a, T b) {
  return (a > b) ? a : b;
}

template <typename T>
constexpr T min_val(T a, T b) {
  return (a < b) ? a : b;
}

esp_err_t http_callback(esp_http_client_event_t* event) {
  esp_err_t err = ESP_OK;
  auto* state = static_cast<RemoteState*>(event->user_data);

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
      ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s",
               event->header_key, event->header_value);

      if (strcasecmp(event->header_key, "Content-Length") == 0) {
        size_t content_length =
            static_cast<size_t>(atoi(event->header_value));
        if (content_length > state->max) {
          ESP_LOGE(TAG,
                   "Content-Length (%zu bytes) exceeds allowed max (%zu bytes)",
                   content_length, state->max);
          if (gfx_display_asset("oversize") != 0) {
            ESP_LOGE(TAG, "Failed to display oversize graphic");
          }
          state->oversize_detected = true;
          err = ESP_ERR_NO_MEM;
          esp_http_client_close(event->client);
        } else {
          ESP_LOGI(TAG, "Content-Length Header: %zu", content_length);
        }
      }

      if (strcasecmp(event->header_key, "Tronbyt-Brightness") == 0) {
        state->brightness =
            static_cast<uint8_t>(atoi(event->header_value));
        ESP_LOGD(TAG, "Tronbyt-Brightness value: %d%%",
                 state->brightness);
      } else if (strcasecmp(event->header_key, "Tronbyt-Dwell-Secs") ==
                 0) {
        state->dwell_secs = atoi(event->header_value);
      } else if (strcasecmp(event->header_key, "Tronbyt-OTA-URL") == 0) {
        if (state->ota_url) free(state->ota_url);
        state->ota_url = strdup(event->header_value);
        ESP_LOGI(TAG, "Found OTA URL: %s", state->ota_url);
      }
      break;

    case HTTP_EVENT_ON_DATA:
      if (!event->user_data) {
        ESP_LOGW(TAG, "Discarding HTTP response due to missing state");
        break;
      }

      if (state->oversize_detected) {
        ESP_LOGD(TAG, "Discarding HTTP data due to oversize detection");
        break;
      }

      if (!state->buf) {
        ESP_LOGD(TAG, "Discarding HTTP data due to freed buffer");
        break;
      }

      if (event->data_len + state->len > state->size) {
        state->size = max_val(
            min_val(state->size * 2, state->max),
            state->len + event->data_len);
        if (state->size > state->max) {
          ESP_LOGE(TAG, "Response size exceeds allowed max (%zu bytes)",
                   state->max);
          if (gfx_display_asset("oversize") != 0) {
            ESP_LOGE(TAG, "Failed to display oversize graphic");
          }
          free(state->buf);
          state->buf = nullptr;
          state->oversize_detected = true;
          err = ESP_ERR_NO_MEM;
          esp_http_client_close(event->client);
          break;
        }

        void* resized = heap_caps_realloc(state->buf, state->size,
                                          MALLOC_CAP_SPIRAM);
        if (!resized) {
          ESP_LOGE(TAG, "Resizing response buffer failed");
          free(state->buf);
          state->buf = nullptr;
          err = ESP_ERR_NO_MEM;
          break;
        }
        state->buf = resized;
      }

      memcpy(static_cast<uint8_t*>(state->buf) + state->len,
             event->data, event->data_len);
      state->len += event->data_len;
      break;

    case HTTP_EVENT_ON_FINISH:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
      break;

    case HTTP_EVENT_DISCONNECTED: {
      ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
      int mbedtls_err = 0;
      esp_err_t tls_err = esp_tls_get_and_clear_last_error(
          static_cast<esp_tls_error_handle_t>(event->data), &mbedtls_err,
          nullptr);
      if (tls_err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP error - %s (mbedtls: 0x%x)",
                 esp_err_to_name(tls_err), mbedtls_err);
      }
    } break;

    case HTTP_EVENT_REDIRECT:
      ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
      esp_http_client_set_redirection(event->client);
      break;

    case HTTP_EVENT_ON_HEADERS_COMPLETE:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADERS_COMPLETE");
      break;

    case HTTP_EVENT_ON_STATUS_CODE:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_STATUS_CODE");
      break;
  }

  return err;
}

}  // namespace

int remote_get(const char* url, uint8_t** buf, size_t* len,
               uint8_t* brightness_pct, int32_t* dwell_secs,
               int* return_status_code, char** ota_url) {
  RemoteState state = {
      .buf = heap_caps_malloc(CONFIG_HTTP_BUFFER_SIZE_DEFAULT,
                              MALLOC_CAP_SPIRAM),
      .len = 0,
      .size = CONFIG_HTTP_BUFFER_SIZE_DEFAULT,
      .max = CONFIG_HTTP_BUFFER_SIZE_MAX,
      .brightness = 255,
      .dwell_secs = -1,
      .ota_url = nullptr,
      .oversize_detected = false,
  };

  if (!state.buf) {
    ESP_LOGE(TAG, "couldn't allocate HTTP receive buffer");
    return 1;
  }

  esp_http_client_config_t config = {};
  config.url = url;
  config.event_handler = http_callback;
  config.user_data = &state;
  config.timeout_ms = 20000;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t http = esp_http_client_init(&config);
  if (!http) {
    ESP_LOGE(TAG, "HTTP client initialization failed for URL: %s", url);
    free(state.buf);
    return 1;
  }

  if (esp_http_client_set_header(http, "X-Firmware-Version",
                                 FIRMWARE_VERSION) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set firmware version header");
  }

  esp_err_t err = esp_http_client_perform(http);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "couldn't reach %s: %s", url, esp_err_to_name(err));
    free(state.buf);
    esp_http_client_cleanup(http);
    return 1;
  }

  if (state.oversize_detected) {
    ESP_LOGI(TAG, "Request aborted due to oversize content");
    free(state.buf);
    free(state.ota_url);
    esp_http_client_cleanup(http);
    *return_status_code = 413;
    return 1;
  }

  int status_code = esp_http_client_get_status_code(http);
  *return_status_code = status_code;
  if (status_code != 200) {
    ESP_LOGE(TAG, "Server returned HTTP status %d", status_code);
    free(state.buf);
    free(state.ota_url);
    esp_http_client_cleanup(http);
    return 1;
  }

  *buf = static_cast<uint8_t*>(state.buf);
  *len = state.len;
  *brightness_pct = state.brightness;
  if (state.dwell_secs > -1 && state.dwell_secs < 300)
    *dwell_secs = state.dwell_secs;
  *ota_url = state.ota_url;

  esp_http_client_cleanup(http);
  return 0;
}
