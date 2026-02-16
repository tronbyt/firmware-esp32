#include "ntp.h"

#include <atomic>
#include <cstring>
#include <ctime>

#include <cJSON.h>
#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "embedded_tz_db.h"

namespace {

const char* TAG = "ntp";
constexpr const char* NVS_NAMESPACE = "ntp_cfg";

// Timezone fetch from IP geolocation API
constexpr const char* TZ_FETCH_URL = "http://ip-api.com/json";
constexpr size_t TZ_RESPONSE_BUFFER_SIZE = 512;
constexpr size_t TZ_FETCH_TASK_STACK = 4096;
constexpr int TZ_FETCH_TASK_PRIORITY = 5;
constexpr int TZ_FETCH_MAX_RETRIES = 2;
constexpr int TZ_FETCH_RETRY_DELAY_MS = 3000;

bool s_initialized = false;
bool s_synced = false;
std::atomic<bool> s_tz_fetch_in_progress{false};

ntp_config_t s_config = {
    .auto_timezone = true,
    .fetch_tz_on_boot = true,
    .timezone = "UTC",
    .ntp_server = "pool.ntp.org",
};

// ── NVS persistence ────────────────────────────────────────────────

void load_config_from_nvs() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
  if (err != ESP_OK) {
    ESP_LOGI(TAG, "NVS namespace not found, using defaults");
    return;
  }

  size_t sz = sizeof(ntp_config_t);
  err = nvs_get_blob(h, "config", &s_config, &sz);
  if (err != ESP_OK) {
    ESP_LOGI(TAG, "Config not found in NVS, using defaults");
  } else {
    ESP_LOGI(TAG, "Loaded config: auto_tz=%d, fetch_on_boot=%d, tz=%s, ntp=%s",
             s_config.auto_timezone, s_config.fetch_tz_on_boot,
             s_config.timezone, s_config.ntp_server);
  }

  nvs_close(h);
}

void save_config_to_nvs() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return;
  }

  err = nvs_set_blob(h, "config", &s_config, sizeof(ntp_config_t));
  if (err == ESP_OK) {
    nvs_commit(h);
    ESP_LOGI(TAG, "Config saved to NVS");
  } else {
    ESP_LOGE(TAG, "Failed to save config: %s", esp_err_to_name(err));
  }

  nvs_close(h);
}

// ── Timezone helpers ───────────────────────────────────────────────

void apply_timezone_local() {
  const char* posix = tz_db_get_posix_str(s_config.timezone);
  if (!posix) posix = "UTC0";
  setenv("TZ", posix, 1);
  tzset();
}

void apply_timezone_from_name(const char* name) {
  if (!name || name[0] == '\0') return;

  ESP_LOGI(TAG, "Applying timezone: %s", name);
  strncpy(s_config.timezone, name, sizeof(s_config.timezone) - 1);
  s_config.timezone[sizeof(s_config.timezone) - 1] = '\0';

  save_config_to_nvs();
  apply_timezone_local();
}

// ── Timezone fetch from IP geolocation ─────────────────────────────

struct tz_response_buffer_t {
  char data[TZ_RESPONSE_BUFFER_SIZE];
  size_t len;
};

esp_err_t tz_http_event_handler(esp_http_client_event_t* evt) {
  if (!evt || !evt->user_data) return ESP_OK;
  auto* buf = static_cast<tz_response_buffer_t*>(evt->user_data);

  switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
      buf->len = 0;
      buf->data[0] = '\0';
      break;
    case HTTP_EVENT_ON_DATA:
      if (evt->data && evt->data_len > 0) {
        size_t avail = sizeof(buf->data) - buf->len - 1;
        size_t copy = (avail < static_cast<size_t>(evt->data_len))
                          ? avail
                          : static_cast<size_t>(evt->data_len);
        if (copy > 0) {
          memcpy(buf->data + buf->len, evt->data, copy);
          buf->len += copy;
          buf->data[buf->len] = '\0';
        }
      }
      break;
    default:
      break;
  }
  return ESP_OK;
}

bool fetch_timezone_from_api() {
  tz_response_buffer_t response{};

  esp_http_client_config_t cfg{};
  cfg.url = TZ_FETCH_URL;
  cfg.event_handler = tz_http_event_handler;
  cfg.user_data = &response;
  cfg.timeout_ms = 5000;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGW(TAG, "Failed to init HTTP client for TZ fetch");
    return false;
  }

  esp_err_t err = esp_http_client_perform(client);
  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || status != 200) {
    ESP_LOGW(TAG, "TZ fetch failed (err=%s, status=%d)", esp_err_to_name(err),
             status);
    return false;
  }

  cJSON* root = cJSON_Parse(response.data);
  if (!root) {
    ESP_LOGW(TAG, "Failed to parse TZ API response");
    return false;
  }

  cJSON* st = cJSON_GetObjectItem(root, "status");
  if (!st || !cJSON_IsString(st) ||
      strcmp(st->valuestring, "success") != 0) {
    ESP_LOGW(TAG, "TZ API returned non-success status");
    cJSON_Delete(root);
    return false;
  }

  cJSON* tz = cJSON_GetObjectItem(root, "timezone");
  if (!tz || !cJSON_IsString(tz) || !tz->valuestring ||
      !tz->valuestring[0]) {
    ESP_LOGW(TAG, "TZ API response missing timezone field");
    cJSON_Delete(root);
    return false;
  }

  ESP_LOGI(TAG, "Fetched timezone from IP geolocation: %s",
           tz->valuestring);
  apply_timezone_from_name(tz->valuestring);

  cJSON_Delete(root);
  return true;
}

void tz_fetch_task(void* /*arg*/) {
  for (int attempt = 0; attempt <= TZ_FETCH_MAX_RETRIES; attempt++) {
    if (attempt > 0) {
      ESP_LOGI(TAG, "TZ fetch retry %d/%d", attempt, TZ_FETCH_MAX_RETRIES);
      vTaskDelay(pdMS_TO_TICKS(TZ_FETCH_RETRY_DELAY_MS));
    }
    if (fetch_timezone_from_api()) break;
  }

  s_tz_fetch_in_progress.store(false);
  vTaskDelete(nullptr);
}

void spawn_tz_fetch_task() {
  bool expected = false;
  if (!s_tz_fetch_in_progress.compare_exchange_strong(expected, true)) {
    ESP_LOGD(TAG, "TZ fetch already in progress");
    return;
  }

  if (xTaskCreate(tz_fetch_task, "tz_fetch", TZ_FETCH_TASK_STACK, nullptr,
                  TZ_FETCH_TASK_PRIORITY, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create TZ fetch task");
    s_tz_fetch_in_progress.store(false);
  }
}

// ── SNTP ───────────────────────────────────────────────────────────

void time_sync_callback(struct timeval* tv) {
  s_synced = true;

  time_t now = tv->tv_sec;
  struct tm* info = localtime(&now);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", info);
  ESP_LOGI(TAG, "Time synchronized: %s", buf);
}

void start_sntp() {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }

  ESP_LOGI(TAG, "Starting SNTP with server: %s", s_config.ntp_server);

  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, s_config.ntp_server);
  esp_sntp_setservername(1, "time.google.com");
  esp_sntp_setservername(2, "time.cloudflare.com");
  esp_sntp_set_time_sync_notification_cb(time_sync_callback);
  esp_sntp_set_sync_interval(3600 * 1000);
  esp_sntp_init();
}

// ── WiFi event handler ────────────────────────────────────────────

void wifi_event_handler(void* /*arg*/, esp_event_base_t base, int32_t id,
                        void* /*data*/) {
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    apply_timezone_local();
    start_sntp();

    if (s_config.auto_timezone && s_config.fetch_tz_on_boot) {
      spawn_tz_fetch_task();
    }
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    s_synced = false;
  }
}

}  // namespace

// ── Public API ─────────────────────────────────────────────────────

void ntp_init() {
  if (s_initialized) return;
  s_initialized = true;

  load_config_from_nvs();

  const char* posix = tz_db_get_posix_str(s_config.timezone);
  if (!posix) posix = "UTC0";
  setenv("TZ", posix, 1);
  tzset();

  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                             wifi_event_handler, nullptr);
  esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                             wifi_event_handler, nullptr);

  ESP_LOGI(TAG, "NTP initialized (tz_db version: %s)", tz_db_get_version());
}

bool ntp_is_synced() { return s_synced; }

void ntp_sync() {
  if (esp_sntp_enabled()) {
    esp_sntp_restart();
  } else {
    start_sntp();
  }
}

ntp_config_t ntp_get_config() { return s_config; }

void ntp_set_config(const ntp_config_t* config) {
  if (!config) return;

  s_config = *config;
  save_config_to_nvs();
  apply_timezone_local();

  if (esp_sntp_enabled()) {
    start_sntp();
  }
}

void ntp_set_auto_timezone(bool enabled) {
  if (s_config.auto_timezone == enabled) return;
  s_config.auto_timezone = enabled;
  if (s_initialized) save_config_to_nvs();
}

bool ntp_get_auto_timezone() { return s_config.auto_timezone; }

void ntp_set_fetch_tz_on_boot(bool enabled) {
  s_config.fetch_tz_on_boot = enabled;
  if (s_initialized) save_config_to_nvs();
}

bool ntp_get_fetch_tz_on_boot() { return s_config.fetch_tz_on_boot; }

void ntp_set_timezone(const char* timezone) {
  if (!timezone) return;

  strncpy(s_config.timezone, timezone, sizeof(s_config.timezone) - 1);
  s_config.timezone[sizeof(s_config.timezone) - 1] = '\0';
  s_config.auto_timezone = false;

  save_config_to_nvs();
  apply_timezone_local();
}

const char* ntp_get_timezone() { return s_config.timezone; }

void ntp_set_server(const char* server) {
  if (!server) return;

  strncpy(s_config.ntp_server, server, sizeof(s_config.ntp_server) - 1);
  s_config.ntp_server[sizeof(s_config.ntp_server) - 1] = '\0';

  save_config_to_nvs();

  if (esp_sntp_enabled()) {
    start_sntp();
  }
}

const char* ntp_get_server() { return s_config.ntp_server; }
