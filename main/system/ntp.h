#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// NTP configuration persisted in NVS.
typedef struct {
  bool auto_timezone;
  bool fetch_tz_on_boot;
  char timezone[64];
  char ntp_server[64];
} ntp_config_t;

/// Load NVS config, set initial TZ, register WiFi event handlers.
void ntp_init(void);

/// Check if time has been synchronized via SNTP.
bool ntp_is_synced(void);

/// Force a time re-sync (restarts SNTP).
void ntp_sync(void);

/// Get a copy of the full NTP configuration.
ntp_config_t ntp_get_config(void);

/// Replace the full NTP configuration and persist it.
void ntp_set_config(const ntp_config_t* config);

void ntp_set_auto_timezone(bool enabled);
bool ntp_get_auto_timezone(void);

void ntp_set_fetch_tz_on_boot(bool enabled);
bool ntp_get_fetch_tz_on_boot(void);

/// Set timezone by IANA name (e.g. "America/New_York"). Disables auto_timezone.
void ntp_set_timezone(const char* timezone);
const char* ntp_get_timezone(void);

void ntp_set_server(const char* server);
const char* ntp_get_server(void);

#ifdef __cplusplus
}
#endif
