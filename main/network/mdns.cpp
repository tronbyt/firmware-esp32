#include "mdns.h"

#include <esp_app_desc.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <mdns.h>

#include "nvs_settings.h"
#include "sdkconfig.h"

namespace {

const char* TAG = "mdns";

bool s_running = false;

const char* board_model() {
#if defined(CONFIG_BOARD_TIDBYT_GEN1)
  return "tidbyt-gen1";
#elif defined(CONFIG_BOARD_TIDBYT_GEN2)
  return "tidbyt-gen2";
#elif defined(CONFIG_BOARD_TRONBYT_S3)
  return "tronbyt-s3";
#elif defined(CONFIG_BOARD_TRONBYT_S3_WIDE)
  return "tronbyt-s3-wide";
#elif defined(CONFIG_BOARD_PIXOTICKER)
  return "pixoticker";
#elif defined(CONFIG_BOARD_MATRIXPORTAL_S3)
  return "matrixportal-s3";
#else
  return "unknown";
#endif
}

void start_mdns() {
  if (s_running) return;

  esp_err_t ret = mdns_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(ret));
    return;
  }

  auto cfg = config_get();
  mdns_hostname_set(cfg.hostname);

  const esp_app_desc_t* app = esp_app_get_description();

  mdns_txt_item_t txt[] = {
      {"model", board_model()},
      {"version", app->version},
  };

  ret = mdns_service_add(nullptr, "_tronbyt", "_tcp", 80, txt, 2);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "mdns_service_add failed: %s", esp_err_to_name(ret));
    mdns_free();
    return;
  }

  s_running = true;
  ESP_LOGI(TAG, "mDNS started: %s.local", cfg.hostname);
}

void stop_mdns() {
  if (!s_running) return;

  mdns_free();
  s_running = false;
  ESP_LOGI(TAG, "mDNS stopped");
}

void event_handler(void*, esp_event_base_t base, int32_t id, void*) {
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    start_mdns();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    stop_mdns();
  }
}

}  // namespace

void mdns_service_init(void) {
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler,
                             nullptr);
  esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                             event_handler, nullptr);
  ESP_LOGI(TAG, "mDNS event handlers registered");
}
