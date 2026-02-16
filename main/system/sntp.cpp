#include "sntp.h"

#include <cstring>

#include <esp_log.h>
#include <esp_sntp.h>

#include "nvs_settings.h"

namespace {
const char* TAG = "sntp";
}  // namespace

void app_sntp_config(void) {
  if (esp_sntp_enabled()) return;
  ESP_LOGI(TAG, "Configuring SNTP");
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

  auto cfg = config_get();
  const char* server = cfg.sntp_server;

  esp_sntp_setservername(1, "pool.ntp.org");
  esp_sntp_setservername(2, "time.google.com");

  if (strlen(server) > 0 && strcmp(server, "pool.ntp.org") != 0) {
    ESP_LOGI(TAG, "Using SNTP server from NVS: %s", server);
    esp_sntp_setservername(0, server);
  } else {
    ESP_LOGI(TAG, "Using SNTP from DHCP (fallback: pool.ntp.org)");
    esp_sntp_servermode_dhcp(1);
    esp_sntp_setservername(0, "pool.ntp.org");
  }
}
