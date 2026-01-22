#pragma once

#include <esp_err.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include "wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Access Point and start services
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ap_start(void);

/**
 * @brief Stop the Access Point services
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ap_stop(void);

/**
 * @brief Initialize the AP network interface
 */
void ap_init_netif(void);

/**
 * @brief Configure the Access Point settings
 */
void ap_configure(void);

/**

 * @brief Start the AP auto-shutdown timer

 */

void ap_start_shutdown_timer(void);

#ifdef __cplusplus
}

#endif
