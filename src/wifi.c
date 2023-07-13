#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <nvs_flash.h>
#include <string.h>

/* The event group allows multiple bits for each event, but we only care about
 * two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_MAXIMUM_RETRY 10

static const char* TAG = "wifi station";

static int s_retry_num = 0;

static EventGroupHandle_t s_wifi_event_group;

static void _wifiHandler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < WIFI_MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "retry to connect to the AP");
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "connect to the AP fail");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

int wifi_initialize(const char* ssid, const char* password) {
  s_wifi_event_group = xEventGroupCreate();

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Netif init failed: %s", esp_err_to_name(err));
    return 1;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Event loop init failed: %s", esp_err_to_name(err));
    return 1;
  }
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Init failed: %s", esp_err_to_name(err));
    return 1;
  }

  esp_event_handler_instance_t instance_any_id;
  err = esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &_wifiHandler, NULL, &instance_any_id);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Event handler init failed: %s", esp_err_to_name(err));
    return 1;
  }

  esp_event_handler_instance_t instance_got_ip;
  err = esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &_wifiHandler, NULL, &instance_got_ip);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Event handler registration failed: %s",
             esp_err_to_name(err));
    return 1;
  }

  wifi_config_t wifi_config = {0};
  strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
  strncpy((char*)wifi_config.sta.password, password,
          sizeof(wifi_config.sta.password));
  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Set mode failed: %s", esp_err_to_name(err));
    return 1;
  }
  err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Set config failed: %s", esp_err_to_name(err));
    return 1;
  }
  err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Start failed: %s", esp_err_to_name(err));
    return 1;
  }

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or
   * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). The
   * bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we
   * can test which event actually happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", ssid, password);
    return 0;
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", ssid, password);
    return 1;
  } else {
    ESP_LOGE(TAG, "Unexpected event");
    return 1;
  }
}

void wifi_shutdown() {
  esp_wifi_stop();
  esp_wifi_deinit();
}

int wifi_get_mac(uint8_t mac[6]) {
  esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get MAC address: %s", esp_err_to_name(err));
    return 1;
  }
  return 0;
}
