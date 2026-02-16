#include <cstring>

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ap.h"
#include "console.h"
#include "display.h"
#include "heap_monitor.h"
#include "http_server.h"
#include "mdns_service.h"
#include "nvs_settings.h"
#include "scheduler.h"
#include "sdkconfig.h"
#include "ntp.h"
#include "sockets.h"
#include "sta_api.h"
#include "syslog.h"
#include "version.h"
#include "webp_player.h"
#include "wifi.h"

#if CONFIG_BUTTON_PIN >= 0
#include <driver/gpio.h>
#endif

namespace {

const char* TAG = "main";
bool button_boot = false;
bool config_received = false;

void config_saved_callback() {
  config_received = true;
  ESP_LOGI(TAG, "Configuration saved - signaling main task");
}

}  // namespace

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "App Main Start");

#if CONFIG_BUTTON_PIN >= 0
  gpio_config_t button_config = {.pin_bit_mask = (1ULL << CONFIG_BUTTON_PIN),
                                 .mode = GPIO_MODE_INPUT,
                                 .pull_up_en = GPIO_PULLUP_ENABLE,
                                 .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                 .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&button_config);

  button_boot = (gpio_get_level(static_cast<gpio_num_t>(CONFIG_BUTTON_PIN)) == 0);

  if (button_boot) {
    ESP_LOGI(TAG, "Boot button pressed - forcing configuration mode");
  } else {
    ESP_LOGI(TAG, "Boot button not pressed");
  }
#else
  ESP_LOGI(TAG, "No button pin defined - skipping button check");
#endif

  ESP_LOGI(TAG, "Check for button press");

  ESP_ERROR_CHECK(nvs_settings_init());
  console_init();
  heap_monitor_init();

  ESP_LOGI(TAG, "Initializing WiFi manager...");
  if (wifi_initialize("", "")) {
    ESP_LOGE(TAG, "failed to initialize WiFi");
    return;
  }
  esp_register_shutdown_handler(&wifi_shutdown);
  http_server_init();
  mdns_service_init();

  auto cfg = config_get();
  const char* image_url = (cfg.image_url[0] != '\0') ? cfg.image_url : nullptr;

  if (gfx_initialize(image_url)) {
    ESP_LOGE(TAG, "failed to initialize gfx");
    return;
  }
  esp_register_shutdown_handler(&display_shutdown);

  if (cfg.ap_mode) {
    ESP_LOGI(TAG, "Starting AP Web Server...");
    ap_start();
  }

  wifi_register_config_callback(config_saved_callback);

  vTaskDelay(pdMS_TO_TICKS(1000));

  uint8_t mac[6];
  if (!wifi_get_mac(mac)) {
    ESP_LOGI(TAG, "WiFi MAC: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
  }

  bool sta_connected = wifi_wait_for_connection(60000);

  if (sta_connected) {
    ESP_LOGI(TAG, "WiFi connected successfully!");

    if (config_get().prefer_ipv6) {
      ESP_LOGI(TAG, "IPv6 preference enabled, waiting for global address...");
      if (wifi_wait_for_ipv6(5000)) {
        ESP_LOGI(TAG, "IPv6 Ready!");
      } else {
        ESP_LOGI(TAG,
                 "IPv6 not available or timed out, proceeding with existing "
                 "connection (IPv4)");
      }
    }

    ntp_init();

    {
      auto syslog_cfg = config_get();
      if (strlen(syslog_cfg.syslog_addr) > 0) {
        syslog_init(syslog_cfg.syslog_addr);
      }
    }

    sta_api_start();
  }

  if (cfg.ap_mode) {
    ap_register_wildcard();
  }

  if (cfg.ap_mode) {
    if (button_boot || !sta_connected) {
      ESP_LOGW(TAG, "WiFi didn't connect or Boot Button Pressed");
      ESP_LOGI(TAG, "Loading Config WEBP");

      if (gfx_display_asset("config")) {
        ESP_LOGE(TAG,
                 "Failed to display config screen - continuing without it");
      }
    }
  } else {
    if (!sta_connected) {
      ESP_LOGW(TAG,
               "WiFi didn't connect and AP mode is disabled - check secrets");
    } else {
      if (button_boot) {
        ESP_LOGW(TAG,
                 "Boot button pressed but AP mode disabled; skipping "
                 "configuration portal");
      }
    }
  }

  if (button_boot) {
    if (!cfg.ap_mode) {
      ESP_LOGW(TAG,
               "Boot button pressed but AP mode disabled; skipping "
               "configuration wait");
    } else {
      ESP_LOGW(TAG,
               "Boot button pressed - waiting for configuration or timeout...");
      int config_wait_counter = 0;
      while (config_wait_counter < 120) {
        if (config_received) {
          ESP_LOGI(TAG, "Configuration received - proceeding");
          break;
        }
        config_wait_counter++;
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }
  } else if (!wifi_is_connected()) {
    ESP_LOGW(TAG, "Pausing main task until wifi connected...");
    int counter = 0;
    while (!wifi_is_connected()) {
      counter++;
      vTaskDelay(pdMS_TO_TICKS(1000));
      if (counter > 600) esp_restart();
    }
  }

  if (cfg.ap_mode) {
    ap_start_shutdown_timer();
  }

  while (true) {
    cfg = config_get();
    image_url = (cfg.image_url[0] != '\0') ? cfg.image_url : nullptr;
    if (image_url && strlen(image_url) > 0) {
      break;
    }
    ESP_LOGW(TAG, "Image URL is not set. Waiting for configuration...");
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  ESP_LOGI(TAG, "Proceeding with image URL: %s", image_url);
  heap_monitor_log_status("pre-connect");

  // Initialize the scheduler (registers player event handlers)
  scheduler_init();

  if (strncmp(image_url, "ws://", 5) == 0 ||
      strncmp(image_url, "wss://", 6) == 0) {
    ESP_LOGI(TAG, "Using websockets with URL: %s", image_url);
    sockets_init(image_url);
    scheduler_start_ws();
  } else {
    ESP_LOGI(TAG, "Using HTTP polling with URL: %s", image_url);
    scheduler_start_http(image_url);
  }

  // All work is now event-driven. Delete this task to free its stack.
  ESP_LOGI(TAG, "Setup complete — deleting app_main task");
  vTaskDelete(nullptr);
}
