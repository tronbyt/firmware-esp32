#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the simple WiFi manager
 *
 * @return ESP_OK on success, or an error code
 */
esp_err_t simple_wifi_manager_init(void);

/**
 * @brief Start the WiFi manager
 *
 * This will start the AP mode and HTTP server for configuration
 *
 * @return ESP_OK on success, or an error code
 */
esp_err_t simple_wifi_manager_start(void);

/**
 * @brief Check if WiFi is connected to an AP
 *
 * @return true if connected, false otherwise
 */
bool simple_wifi_manager_is_connected(void);

/**
 * @brief Wait for WiFi connection with timeout
 *
 * @param timeout_ms Timeout in milliseconds
 * @return true if connected, false if timeout
 */
bool simple_wifi_manager_wait_for_connection(uint32_t timeout_ms);

/**
 * @brief Get the current image URL
 *
 * @return Pointer to the image URL string, or NULL if not set
 */
char* simple_wifi_manager_get_image_url(void);

/**
 * @brief Register a callback to be called when WiFi connects
 *
 * @param callback Function to call when WiFi connects
 */
void simple_wifi_manager_register_connect_callback(void (*callback)(void));

/**
 * @brief Register a callback to be called when WiFi disconnects
 *
 * @param callback Function to call when WiFi disconnects
 */
void simple_wifi_manager_register_disconnect_callback(void (*callback)(void));

#ifdef __cplusplus
}
#endif
