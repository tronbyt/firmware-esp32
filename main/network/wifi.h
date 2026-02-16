#pragma once

#include <esp_err.h>
#include <esp_http_server.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi
 *
 * @param ssid SSID (not used, kept for compatibility)
 * @param password Password (not used, kept for compatibility)
 * @return 0 on success, non-zero on failure
 */
int wifi_initialize(const char *ssid, const char *password);

/**
 * @brief Shutdown WiFi
 */
void wifi_shutdown(void);

/**
 * @brief Get MAC address
 *
 * @param mac Buffer to store MAC address (6 bytes)
 * @return 0 on success, non-zero on failure
 */
int wifi_get_mac(uint8_t mac[6]);

/**
 * @brief Set Hostname
 *
 * @param hostname Hostname to set
 * @return 0 on success, non-zero on failure
 */
int wifi_set_hostname(const char *hostname);

/**
 * @brief Wait for WiFi connection with timeout
 *
 * @param timeout_ms Timeout in milliseconds
 * @return true if connected, false if timeout
 */
bool wifi_wait_for_connection(uint32_t timeout_ms);

/**
 * @brief Wait for IPv6 address with timeout
 *
 * @param timeout_ms Timeout in milliseconds
 * @return true if IPv6 address acquired, false if timeout
 */
bool wifi_wait_for_ipv6(uint32_t timeout_ms);

/**
 * @brief Check if WiFi is connected
 *
 * @return true if connected, false otherwise
 */
bool wifi_is_connected(void);

// Add new function to register config callback
void wifi_register_config_callback(void (*callback)(void));

/**
 * @brief Check WiFi health and reconnect if needed
 */
void wifi_health_check(void);

/**
 * @brief Apply power save mode from settings
 */
void wifi_apply_power_save(void);

#ifdef __cplusplus
}
#endif
