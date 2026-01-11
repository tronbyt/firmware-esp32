#include "sntp.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs_settings.h"

static const char* TAG = "sntp";

void app_sntp_config(void) {
  if (esp_sntp_enabled()) return;
  ESP_LOGI(TAG, "Configuring SNTP");
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

  char server[MAX_SNTP_SERVER_LEN + 1] = {0};
  nvs_get_sntp_server(server, sizeof(server));

  // Set secondary fallbacks first (higher indexes)
  esp_sntp_setservername(1, "pool.ntp.org");
  esp_sntp_setservername(2, "time.google.com");

  if (strlen(server) > 0 && strcmp(server, "pool.ntp.org") != 0) {
    // 1. Use NVS value if set and NOT default pool
    ESP_LOGI(TAG, "Using SNTP server from NVS: %s", server);
    esp_sntp_setservername(0, server);
  } else {
    // 2. Use DHCP (if enabled)
    ESP_LOGI(TAG, "Using SNTP from DHCP (fallback: pool.ntp.org)");
    esp_sntp_servermode_dhcp(1);
    esp_sntp_setservername(0, "pool.ntp.org");
  }
}
