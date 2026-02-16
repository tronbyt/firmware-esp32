#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64
#define MAX_HOSTNAME_LEN 32
#define MAX_URL_LEN 512
#define MAX_IP_LEN 64
#define MAX_SYSLOG_ADDR_LEN 128
#define MAX_SNTP_SERVER_LEN 64

typedef struct {
  char ssid[MAX_SSID_LEN + 1];
  char password[MAX_PASSWORD_LEN + 1];
  char hostname[MAX_HOSTNAME_LEN + 1];
  char syslog_addr[MAX_SYSLOG_ADDR_LEN + 1];
  char sntp_server[MAX_SNTP_SERVER_LEN + 1];
  char image_url[MAX_URL_LEN + 1];
  bool swap_colors;
  wifi_ps_type_t wifi_power_save;
  bool skip_display_version;
  bool ap_mode;
  bool prefer_ipv6;
} system_config_t;

/// Initialize NVS and load settings into the config struct.
esp_err_t nvs_settings_init(void);

/// Return a thread-safe copy of the current configuration.
system_config_t config_get(void);

/// Apply a new configuration and persist it to NVS.
void config_set(const system_config_t* cfg);

#ifdef __cplusplus
}
#endif
