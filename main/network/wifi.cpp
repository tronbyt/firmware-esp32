#include "wifi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <lwip/dns.h>
#include <lwip/err.h>
#include <lwip/ip4_addr.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "ap.h"
#include "nvs_settings.h"
#include "sntp.h"

namespace {

const char* TAG = "WIFI";

// Event group bits
constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;
constexpr EventBits_t WIFI_FAIL_BIT = BIT1;
constexpr EventBits_t WIFI_CONNECTED_IPV6_BIT = BIT2;

constexpr int MAX_RECONNECT_ATTEMPTS = 10;

EventGroupHandle_t s_wifi_event_group = nullptr;
esp_netif_t* s_sta_netif = nullptr;
void (*s_config_callback)(void) = nullptr;

int s_reconnect_attempts = 0;
bool s_connection_given_up = false;
int s_wifi_disconnect_counter = 0;

void handle_successful_ip_acquisition() {
  s_reconnect_attempts = 0;
  s_connection_given_up = false;
  xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
  xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
}

void wifi_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data) {
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START:
        s_reconnect_attempts = 0;
        s_connection_given_up = false;
        esp_wifi_connect();
        break;
      case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Connected to AP, creating IPv6 link local address");
        esp_netif_create_ip6_linklocal(s_sta_netif);
        break;
      case WIFI_EVENT_STA_DISCONNECTED:
        s_reconnect_attempts++;
        xEventGroupClearBits(s_wifi_event_group,
                             WIFI_CONNECTED_BIT | WIFI_CONNECTED_IPV6_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);

        if (config_get().ap_mode &&
            s_reconnect_attempts >= MAX_RECONNECT_ATTEMPTS &&
            !s_connection_given_up) {
          ESP_LOGW(TAG,
                   "Maximum reconnection attempts (%d) reached, giving up",
                   MAX_RECONNECT_ATTEMPTS);
          s_connection_given_up = true;
        } else if (!s_connection_given_up) {
          ESP_LOGI(TAG,
                   "WiFi disconnected, trying to reconnect... (attempt %d)",
                   s_reconnect_attempts);
          esp_wifi_connect();
        }
        break;
      case WIFI_EVENT_AP_STACONNECTED: {
        auto* event =
            static_cast<wifi_event_ap_staconnected_t*>(event_data);
        ESP_LOGI(TAG, "Station joined, AID=%d", event->aid);
      } break;
      case WIFI_EVENT_AP_STADISCONNECTED: {
        auto* event =
            static_cast<wifi_event_ap_stadisconnected_t*>(event_data);
        ESP_LOGI(TAG, "Station left, AID=%d", event->aid);
      } break;
      default:
        break;
    }
  } else if (event_base == IP_EVENT) {
    switch (event_id) {
      case IP_EVENT_STA_GOT_IP: {
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        ESP_LOGI(TAG, "Got IP address: " IPSTR,
                 IP2STR(&event->ip_info.ip));
        handle_successful_ip_acquisition();
      } break;
      case IP_EVENT_GOT_IP6: {
        auto* event = static_cast<ip_event_got_ip6_t*>(event_data);
        auto* addr =
            reinterpret_cast<ip6_addr_t*>(&event->ip6_info.ip);
        ESP_LOGI(TAG, "Got IPv6 address: " IPV6STR,
                 IPV62STR(event->ip6_info.ip));

        if (ip6_addr_isglobal(addr)) {
          ESP_LOGI(TAG, "IPv6 address acquired");
          xEventGroupSetBits(s_wifi_event_group,
                             WIFI_CONNECTED_IPV6_BIT);
          handle_successful_ip_acquisition();
        } else {
          ESP_LOGI(TAG, "IPv6 address is not global, waiting...");
        }
      } break;
      default:
        break;
    }
  }
}

}  // namespace

int wifi_initialize(const char* ssid, const char* password) {
  ESP_LOGI(TAG, "Initializing WiFi");

  if (!config_get().ap_mode) {
    ESP_LOGI(TAG, "AP mode disabled via settings");
  }

  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  app_sntp_config();

  s_sta_netif = esp_netif_create_default_wifi_sta();
  auto settings = config_get();
  if (settings.ap_mode) {
    ap_init_netif();
  }

  wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

  char hostname[MAX_HOSTNAME_LEN + 1];
  snprintf(hostname, sizeof(hostname), "%s", settings.hostname);
  if (strlen(hostname) == 0) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(hostname, sizeof(hostname), "tronbyt-%02x%02x%02x", mac[3],
             mac[4], mac[5]);
    ESP_LOGI(TAG, "Generated default hostname: %s", hostname);
    snprintf(settings.hostname, sizeof(settings.hostname), "%s", hostname);
    config_set(&settings);
  }
  wifi_set_hostname(hostname);

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifi_event_handler, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6,
                                             &wifi_event_handler, nullptr));

  bool has_credentials = (strlen(settings.ssid) > 0);

  if (settings.ap_mode) {
    ap_configure();
  } else {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  }

  if (has_credentials) {
    wifi_config_t sta_config = {};
    snprintf(reinterpret_cast<char*>(sta_config.sta.ssid),
             sizeof(sta_config.sta.ssid), "%s", settings.ssid);
    snprintf(reinterpret_cast<char*>(sta_config.sta.password),
             sizeof(sta_config.sta.password), "%s", settings.password);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_LOGI(TAG, "Configured STA with SSID: %s", settings.ssid);
  }

  ESP_ERROR_CHECK(esp_wifi_start());
  wifi_apply_power_save();

  int8_t tx_power;
  ESP_ERROR_CHECK(esp_wifi_get_max_tx_power(&tx_power));
  ESP_LOGI(TAG, "Max TX Power (Current): %.2f dBm", tx_power * 0.25f);

#ifdef CONFIG_IDF_TARGET_ESP32S3
  ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(44));
  ESP_ERROR_CHECK(esp_wifi_get_max_tx_power(&tx_power));
  ESP_LOGI(TAG, "Max TX Power (S3 limit applied): %.2f dBm",
           tx_power * 0.25f);
#endif

  if (!has_credentials) {
    if (settings.ap_mode) {
      ESP_LOGI(
          TAG,
          "No valid WiFi credentials available, starting in AP mode only");
    } else {
      ESP_LOGW(
          TAG,
          "No valid WiFi credentials available and AP mode is disabled");
    }
    s_reconnect_attempts = MAX_RECONNECT_ATTEMPTS;
    s_connection_given_up = true;
  }

  ESP_LOGI(TAG, "WiFi initialized successfully");
  return 0;
}

void wifi_shutdown(void) {
  ap_stop();
  esp_wifi_stop();
  esp_wifi_deinit();

  esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               &wifi_event_handler);
  esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifi_event_handler);
  esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6,
                               &wifi_event_handler);

  if (s_wifi_event_group) {
    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = nullptr;
  }
}

int wifi_get_mac(uint8_t mac[6]) {
  esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get MAC address: %s", esp_err_to_name(err));
    return 1;
  }
  return 0;
}

int wifi_set_hostname(const char* hostname) {
  if (s_sta_netif) {
    esp_err_t err = esp_netif_set_hostname(s_sta_netif, hostname);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Hostname set to: %s", hostname);
      return 0;
    }
    ESP_LOGE(TAG, "Failed to set hostname: %s", esp_err_to_name(err));
  }
  return 1;
}

bool wifi_is_connected(void) {
  return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

bool wifi_wait_for_connection(uint32_t timeout_ms) {
  ESP_LOGI(TAG, "Waiting for WiFi connection (timeout: %lu ms)",
           static_cast<unsigned long>(timeout_ms));

  if (wifi_is_connected()) {
    ESP_LOGI(TAG, "Already connected to WiFi");
    return true;
  }

  if (strlen(config_get().ssid) == 0) {
    ESP_LOGI(TAG, "No saved config, won't connect.");
    return false;
  }

  TickType_t start_ticks = xTaskGetTickCount();
  TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

  while (xTaskGetTickCount() - start_ticks < timeout_ticks) {
    if (wifi_is_connected()) {
      ESP_LOGI(TAG, "Connected to WiFi");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  ESP_LOGW(TAG, "WiFi connection timeout");
  return false;
}

bool wifi_wait_for_ipv6(uint32_t timeout_ms) {
  if (!s_wifi_event_group) return false;

  if (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_IPV6_BIT) {
    return true;
  }

  ESP_LOGI(TAG, "Waiting for IPv6 address (timeout: %lu ms)",
           static_cast<unsigned long>(timeout_ms));
  EventBits_t bits = xEventGroupWaitBits(
      s_wifi_event_group, WIFI_CONNECTED_IPV6_BIT, pdFALSE, pdTRUE,
      pdMS_TO_TICKS(timeout_ms));

  if (bits & WIFI_CONNECTED_IPV6_BIT) {
    return true;
  }

  ESP_LOGI(TAG, "IPv6 address wait timeout");
  return false;
}

void wifi_register_config_callback(void (*callback)(void)) {
  s_config_callback = callback;
}

void wifi_health_check(void) {
  if (wifi_is_connected()) {
    if (s_wifi_disconnect_counter > 0) {
      s_wifi_disconnect_counter = 0;
    }
    return;
  }

  s_wifi_disconnect_counter++;
  ESP_LOGW(TAG, "WiFi Health check. Disconnect count: %d",
           s_wifi_disconnect_counter);

  if (s_wifi_disconnect_counter >= 10) {
    ESP_LOGE(TAG, "WiFi disconnect count reached %d - rebooting",
             s_wifi_disconnect_counter);
    esp_restart();
  }

  if (strlen(config_get().ssid) > 0) {
    ESP_LOGI(TAG, "Reconnecting in Health check...");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "WiFi reconnect attempt failed: %s",
               esp_err_to_name(err));
    }
  } else {
    ESP_LOGW(TAG, "No SSID configured, cannot reconnect");
  }
}

void wifi_apply_power_save(void) {
  wifi_ps_type_t power_save_mode = config_get().wifi_power_save;
  ESP_LOGI(TAG, "Setting WiFi Power Save Mode to %d...", power_save_mode);
  esp_wifi_set_ps(power_save_mode);
}
