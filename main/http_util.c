#include "http_util.h"

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <string.h>

#include "version.h"

static const char *TAG = "http_util";

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
  size_t max;
  bool overflow;
} collect_t;

static esp_err_t on_event(esp_http_client_event_t *evt) {
  collect_t *c = evt->user_data;
  if (evt->event_id != HTTP_EVENT_ON_DATA || c->overflow) return ESP_OK;
  if (c->len + evt->data_len + 1 > c->max) {
    c->overflow = true;
    return ESP_OK;
  }
  if (c->len + evt->data_len + 1 > c->cap) {
    size_t cap = c->cap ? c->cap * 2 : 8192;
    while (cap < c->len + evt->data_len + 1) cap *= 2;
    if (cap > c->max) cap = c->max;
    char *grown = heap_caps_realloc(c->buf, cap, MALLOC_CAP_SPIRAM);
    if (!grown) {
      c->overflow = true;
      return ESP_OK;
    }
    c->buf = grown;
    c->cap = cap;
  }
  memcpy(c->buf + c->len, evt->data, evt->data_len);
  c->len += evt->data_len;
  c->buf[c->len] = '\0';
  return ESP_OK;
}

esp_err_t http_request(const char *method, const char *url,
                       const char *const *headers, const char *body,
                       size_t max_len, http_response_t *out) {
  memset(out, 0, sizeof(*out));
  collect_t c = {.max = max_len};

  esp_http_client_config_t cfg = {
      .url = url,
      .event_handler = on_event,
      .user_data = &c,
      .timeout_ms = 10000,
      .buffer_size = 4096,
      .buffer_size_tx = 2048,  // room for long Authorization headers
      .crt_bundle_attach = esp_crt_bundle_attach,
      .user_agent = "tidbyt-local-apps/" FIRMWARE_VERSION,
  };
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) return ESP_FAIL;

  if (strcmp(method, "POST") == 0) {
    esp_http_client_set_method(client, HTTP_METHOD_POST);
  } else if (strcmp(method, "PUT") == 0) {
    esp_http_client_set_method(client, HTTP_METHOD_PUT);
  }
  for (int i = 0; headers && headers[i] && headers[i + 1]; i += 2) {
    esp_http_client_set_header(client, headers[i], headers[i + 1]);
  }
  if (body) esp_http_client_set_post_field(client, body, strlen(body));

  esp_err_t err = esp_http_client_perform(client);
  out->status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || c.overflow) {
    ESP_LOGW(TAG, "%s %s failed: %s%s", method, url, esp_err_to_name(err),
             c.overflow ? " (response too large)" : "");
    free(c.buf);
    out->status = 0;
    return err != ESP_OK ? err : ESP_ERR_NO_MEM;
  }
  out->body = c.buf;
  out->len = c.len;
  return ESP_OK;
}

void http_response_free(http_response_t *resp) {
  free(resp->body);
  resp->body = NULL;
  resp->len = 0;
}
