#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// WireGuard modes (stored in NVS as "wg_mode")
#define WG_MODE_OFF 0
#define WG_MODE_AUTO 1    // tunnel only if the server is not reachable directly
#define WG_MODE_ALWAYS 2  // always use the tunnel

/**
 * @brief Start the WireGuard manager.
 *
 * Does nothing if WireGuard is off or no configuration is stored. Otherwise a
 * background task decides whether the tunnel is needed (mode "auto": only if
 * the host of image_url cannot be reached directly), brings it up and keeps it
 * alive. The call blocks until that first decision is made or wait_ms passed.
 *
 * @param image_url URL of the Tronbyt server (used for the reachability test)
 * @param wait_ms   maximum time to block
 */
void wg_start(const char* image_url, int wait_ms);

/** @return true while the tunnel is up. */
bool wg_is_up(void);

/** Send off-LAN traffic through the tunnel (paired with wg_route_release). */
void wg_route_hold(void);

/** Restore normal WiFi routing after wg_route_hold(). */
void wg_route_release(void);

/**
 * @brief Check a WireGuard configuration (.conf text) without applying it.
 *
 * @return true if it has everything needed to bring a tunnel up
 */
bool wg_conf_is_valid(const char* text);

#ifdef __cplusplus
}
#endif
