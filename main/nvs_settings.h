#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize NVS settings
esp_err_t nvs_settings_init(void);

// Getters
esp_err_t nvs_get_ssid(char *ssid, size_t max_len);
esp_err_t nvs_get_password(char *password, size_t max_len);
const char* nvs_get_image_url(void);
bool nvs_get_swap_colors(void);
wifi_ps_type_t nvs_get_wifi_power_save(void);
bool nvs_get_skip_display_version(void);
bool nvs_get_ap_mode(void);
bool nvs_get_prefer_ipv6(void);

// Setters
esp_err_t nvs_set_ssid(const char *ssid);
esp_err_t nvs_set_password(const char *password);
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
