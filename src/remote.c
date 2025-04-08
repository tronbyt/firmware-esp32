#include <stdbool.h>
#include <stdlib.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_tls.h>

static const char* TAG = "remote";
bool is_local_address(const char* url);

struct remote_state {
  void* buf;
  size_t len;
  size_t size;
  size_t max;
  int32_t brightness;
  int32_t dwell_secs;
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
      if (strcmp(event->header_key, "Content-Length") == 0) {
        size_t content_length = (size_t)atoi(event->header_value);
        if (content_length > state->max) {
          ESP_LOGE(TAG,
                   "Content-Length (%d bytes) exceeds allowed max (%d bytes)",
                   content_length, state->max);
          err = ESP_ERR_NO_MEM;
          esp_http_client_close(event->client);  // Abort the HTTP request
        } else {
          ESP_LOGI(TAG, "Content-Length Header : %d", content_length);
        }
      }

      // Check for the specific header key
      if (strcmp(event->header_key, "Tronbyt-Brightness") == 0) {
        state->brightness = (int)atoi(event->header_value);
        // ESP_LOGI(TAG, "Tronbyt-Brightness value: %i", brightness_value);
      }
      else if (strcmp(event->header_key, "Tronbyt-Dwell-Secs") == 0) {
        state->dwell_secs = (int)atoi(event->header_value);
        // ESP_LOGI(TAG, "Tronbyt-Dwell-Secs value: %i", dwell_secs_value);
      }
      break;

    case HTTP_EVENT_ON_DATA:

      if (event->user_data == NULL) {
        ESP_LOGW(TAG, "Discarding HTTP response due to missing state");
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

int remote_get(const char* url, uint8_t** buf, size_t* len, int* b_int, int32_t* dwell_secs) {
  // State for processing the response
  struct remote_state state = {
      .buf = malloc(HTTP_BUFFER_SIZE_DEFAULT),
      .len = 0,
      .size = HTTP_BUFFER_SIZE_DEFAULT,
      .max = HTTP_BUFFER_SIZE_MAX,
      .brightness = -1,
      .dwell_secs = -1,
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

  };

  if (is_local_address(url)) {
    ESP_LOGI(TAG, "local address, skipping cert validation");
  } else {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }

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

  int status_code = esp_http_client_get_status_code(http);
  if (status_code != 200) {
    ESP_LOGE(TAG, "Server returned HTTP status 500");
    if (state.buf != NULL) {
      free(state.buf);
    }
    esp_http_client_cleanup(http);
    return 1;
  }

  // Write back the results.
  *buf = state.buf;
  *len = state.len;
  if (state.brightness > -1 && state.brightness < 255) *b_int = state.brightness;
  if (state.dwell_secs > -1 && state.dwell_secs < 300) *dwell_secs = state.dwell_secs; // 5 minute max ?

  esp_http_client_cleanup(http);
  ESP_LOGI(TAG,"fetched new webp");
  return 0;
}


bool is_local_address(const char* url) {
  ESP_LOGI(TAG, "URL: %s", url);
  if (strstr(url, ".local:") != NULL) return true;
  if (strstr(url, ".local/") != NULL) return true;
  if (strstr(url, "//10.") != NULL) return true;
  if (strstr(url, "//192.") != NULL) return true;
  if (strstr(url, "//172.") != NULL) return true;
  return false;
  // // Skip protocol prefix if present
  // const char* http_prefix = "http://";
  // const char* https_prefix = "https://";

  // if (strncmp(url, http_prefix, strlen(http_prefix)) == 0) {
  //   url += strlen(http_prefix);
  // } else if (strncmp(url, https_prefix, strlen(https_prefix)) == 0) {
  //   url += strlen(https_prefix);
  // }

  // // Make a copy of the host part (without the port)
  // char host[256];
  // strncpy(host, url, sizeof(host) - 1);
  // host[sizeof(host) - 1] = '\0';

  // // Remove port number if present
  // char* port = strchr(host, ':');
  // if (port != NULL) {
  //   *port = '\0';  // Terminate string at the colon
  // }

  // // Check for .local TLD
  // size_t len = strlen(host);
  // if (len >= 6 && strcmp(host + len - 6, ".local") == 0) {
  //   return true;
  // }

  // // Check if starts with common local IP prefixes
  // if (strncmp(host, "10.", 3) == 0) {
  //   return true;
  // }

  // if (strncmp(host, "192.168.", 8) == 0) {
  //   return true;
  // }

  // if (strncmp(host, "172.", 4) == 0) {
  //   // Check for 172.16.0.0 to 172.31.255.255 range
  //   char* end;
  //   long second_octet = strtol(host + 4, &end, 10);
  //   if (*end == '.' && second_octet >= 16 && second_octet <= 31) {
  //     return true;
  //   }
  // }

  // if (strncmp(host, "127.", 4) == 0) {
  //   return true;
  // }

  // return false;
}
