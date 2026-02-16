#include "http_server.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>

namespace {

const char *TAG = "http_server";

constexpr int MAX_REGISTRARS = 8;

httpd_handle_t s_server = nullptr;
http_handler_registrar_fn s_registrars[MAX_REGISTRARS] = {};
int s_registrar_count = 0;

void invoke_registrars() {
  for (int i = 0; i < s_registrar_count; i++) {
    s_registrars[i](s_server);
  }
}

void on_connect(void *arg, esp_event_base_t event_base, int32_t event_id,
                void *event_data) {
  if (s_server) {
    ESP_LOGD(TAG, "Server already running");
    return;
  }
  ESP_LOGI(TAG, "STA got IP — starting HTTP server");
  http_server_start();
}

}  // namespace

void http_server_init(void) {
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_connect,
                             nullptr);
}

void http_server_start(void) {
  if (s_server) {
    ESP_LOGD(TAG, "Server already running");
    return;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 16;
  config.max_resp_headers = 16;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;

  esp_err_t err = httpd_start(&s_server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
    return;
  }

  ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
  invoke_registrars();
}

void http_server_stop(void) {
  if (!s_server) {
    return;
  }
  httpd_stop(s_server);
  s_server = nullptr;
  ESP_LOGI(TAG, "HTTP server stopped");
}

httpd_handle_t http_server_handle(void) { return s_server; }

void http_server_register_handlers(http_handler_registrar_fn registrar) {
  if (s_registrar_count >= MAX_REGISTRARS) {
    ESP_LOGE(TAG, "Too many registrars (max %d)", MAX_REGISTRARS);
    return;
  }
  s_registrars[s_registrar_count++] = registrar;

  if (s_server) {
    registrar(s_server);
  }
}
