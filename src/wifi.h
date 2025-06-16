#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <esp_http_server.h>

/**
 * @brief Initialize WiFi
 *
 * @param ssid SSID (not used, kept for compatibility)
 * @param password Password (not used, kept for compatibility)
 * @return 0 on success, non-zero on failure
 */
int wifi_initialize(const char *ssid, const char *password);


/**
 * @brief Shutdown WiFi Config Portal
 */
void wifi_shutdown_ap();

/**
 * @brief Shutdown WiFi
 */
void wifi_shutdown();

/**
 * @brief Get MAC address
 *
 * @param mac Buffer to store MAC address (6 bytes)
 * @return 0 on success, non-zero on failure
 */
int wifi_get_mac(uint8_t mac[6]);

/**
 * @brief Wait for WiFi connection with timeout
 *
 * @param timeout_ms Timeout in milliseconds
 * @return true if connected, false if timeout
 */
bool wifi_wait_for_connection(uint32_t timeout_ms);

/**
 * @brief Get the image URL from WiFi manager
 *
 * @return Pointer to image URL string, or NULL if not set
 */
const char* wifi_get_image_url();

/**
 * @brief Check if WiFi is connected to an AP
 *
 * @return true if connected, false otherwise
 */
bool wifi_is_connected(void);

/**
 * @brief Register a callback to be called when WiFi connects
 *
 * @param callback Function to call when WiFi connects
 */
void wifi_register_connect_callback(void (*callback)(void));

/**
 * @brief Register a callback to be called when WiFi disconnects
 *
 * @param callback Function to call when WiFi disconnects
 */
void wifi_register_disconnect_callback(void (*callback)(void));
