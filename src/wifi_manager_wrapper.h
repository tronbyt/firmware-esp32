#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the WiFi manager
 */
void wifi_manager_init(void);

/**
 * Check if WiFi is connected
 * @return true if connected, false otherwise
 */
bool wifi_manager_is_connected(void);

/**
 * Get the MAC address of the WiFi interface
 * @param mac Pointer to a 6-byte array to store the MAC address
 */
void wifi_manager_get_mac(uint8_t* mac);

/**
 * Force the WiFi manager to start the AP mode
 * This can be used to manually trigger the AP mode if the WiFi connection fails
 */
void wifi_manager_start_ap(void);

#ifdef __cplusplus
}
#endif
