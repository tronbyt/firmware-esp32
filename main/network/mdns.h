#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Register WiFi event handlers that start/stop mDNS automatically.
/// mDNS starts when an IP is acquired and stops on disconnect.
void mdns_service_init(void);

#ifdef __cplusplus
}
#endif
