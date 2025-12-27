#include "ota.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_crt_bundle.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

static const char *TAG = "OTA";

void run_ota(const char* url) {
    ESP_LOGI(TAG, "Starting OTA update from URL: %s", url);

    // Sync otadata if we are running in a fallback state
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();

    if (running && (boot == NULL || running->address != boot->address)) {
        ESP_LOGW(TAG, "Running partition (0x%lx) != Boot partition (0x%lx). Syncing otadata...", 
                 (unsigned long)running->address, boot ? (unsigned long)boot->address : 0);
        esp_err_t err = esp_ota_set_boot_partition(running);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to sync otadata: %s. Aborting OTA.", esp_err_to_name(err));
            return;
        } else {
            ESP_LOGI(TAG, "Otadata synced to running partition");
        }
    }

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 60000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .partial_http_download = true,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Update successful. Rebooting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA Update failed");
    }
}
