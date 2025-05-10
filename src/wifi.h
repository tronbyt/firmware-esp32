#pragma once

#include <stdint.h>

/**
 * @brief Initialize WiFi
 *
 * @param ssid SSID (not used with simple-wifi-manager, kept for compatibility)
 * @param password Password (not used with simple-wifi-manager, kept for compatibility)
 * @return 0 on success, non-zero on failure
 */
int wifi_initialize(const char *ssid, const char *password);

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
