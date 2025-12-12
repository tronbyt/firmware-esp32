#include "ota.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_crt_bundle.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "OTA";

void run_ota(const char* url) {
    ESP_LOGI(TAG, "Starting OTA update from URL: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Update successful. Rebooting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA Update failed");
    }
}
