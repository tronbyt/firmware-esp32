#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum string lengths
#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64
#define MAX_HOSTNAME_LEN 32
#define MAX_URL_LEN 128
#define MAX_IP_LEN 64
#define MAX_SYSLOG_ADDR_LEN 128
#define MAX_SNTP_SERVER_LEN 64

// Initialize NVS settings
esp_err_t nvs_settings_init(void);

// Getters
esp_err_t nvs_get_ssid(char *ssid, size_t max_len);
esp_err_t nvs_get_password(char *password, size_t max_len);
esp_err_t nvs_get_hostname(char *hostname, size_t max_len);
esp_err_t nvs_get_syslog_addr(char *addr, size_t max_len);
esp_err_t nvs_get_sntp_server(char *server, size_t max_len);
const char *nvs_get_image_url(void);
bool nvs_get_swap_colors(void);
wifi_ps_type_t nvs_get_wifi_power_save(void);
bool nvs_get_skip_display_version(void);
bool nvs_get_ap_mode(void);
bool nvs_get_prefer_ipv6(void);

// Setters
esp_err_t nvs_set_ssid(const char *ssid);
esp_err_t nvs_set_password(const char *password);
esp_err_t nvs_set_hostname(const char *hostname);
esp_err_t nvs_set_syslog_addr(const char *addr);
esp_err_t nvs_set_sntp_server(const char *server);
esp_err_t nvs_set_image_url(const char *image_url);
esp_err_t nvs_set_swap_colors(bool swap_colors);
esp_err_t nvs_set_wifi_power_save(wifi_ps_type_t power_save);
esp_err_t nvs_set_skip_display_version(bool skip);
esp_err_t nvs_set_ap_mode(bool ap_mode);
esp_err_t nvs_set_prefer_ipv6(bool prefer_ipv6);

// Save all modified settings to NVS
esp_err_t nvs_save_settings(void);

#ifdef __cplusplus
}
#endif
