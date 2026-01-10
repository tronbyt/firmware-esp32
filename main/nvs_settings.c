#include "nvs_settings.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "esp_wifi_types.h"

#define TAG "NVS_SETTINGS"

// NVS namespace and keys
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"
#define NVS_KEY_HOSTNAME "hostname"
#define NVS_KEY_IMAGE_URL "image_url"
#define NVS_KEY_SWAP_COLORS "swap_colors"
#define NVS_KEY_WIFI_POWER_SAVE "wifi_ps"
#define NVS_KEY_SKIP_VERSION "skip_ver"
#define NVS_KEY_AP_MODE "ap_mode"
#define NVS_KEY_PREFER_IPV6 "prefer_ipv6"

// Internal storage
static char s_wifi_ssid[MAX_SSID_LEN + 1] = {0};
static char s_wifi_password[MAX_PASSWORD_LEN + 1] = {0};
static char s_hostname[MAX_HOSTNAME_LEN + 1] = {0};
static char s_image_url[MAX_URL_LEN + 1] = {0};
static bool s_swap_colors = false;
static wifi_ps_type_t s_wifi_power_save = WIFI_PS_MIN_MODEM;
static bool s_skip_display_version = false;
static bool s_ap_mode = true;
static bool s_prefer_ipv6 = false;

// Hardcoded defaults (from secrets.json via CMake)
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef REMOTE_URL
#define REMOTE_URL ""
#endif
#ifndef SWAP_COLORS_DEFAULT
#define SWAP_COLORS_DEFAULT 0
#endif
#ifndef WIFI_POWER_SAVE_MODE
#define WIFI_POWER_SAVE_MODE WIFI_PS_MIN_MODEM
#endif
#ifndef AP_MODE_DEFAULT
#define AP_MODE_DEFAULT 1
#endif
#ifndef ENABLE_AP_MODE
#define ENABLE_AP_MODE 1
#endif
#ifndef PREFER_IPV6
#define PREFER_IPV6 0
#endif

esp_err_t nvs_settings_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;

    nvs_handle_t nvs_handle;
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    
    // Set defaults first
    #ifdef SWAP_COLORS_DEFAULT
    s_swap_colors = true;
    #else
    s_swap_colors = false;
    #endif
    s_wifi_power_save = WIFI_POWER_SAVE_MODE;
    s_skip_display_version = SKIP_DISPLAY_VERSION;
    s_ap_mode = AP_MODE_DEFAULT;
    s_prefer_ipv6 = PREFER_IPV6;

    if (ret == ESP_OK) {
        // Load from NVS
        size_t required_size = sizeof(s_wifi_ssid);
        if (nvs_get_str(nvs_handle, NVS_KEY_SSID, s_wifi_ssid, &required_size) != ESP_OK) {
            s_wifi_ssid[0] = '\0';
        }

        required_size = sizeof(s_wifi_password);
        if (nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, s_wifi_password, &required_size) != ESP_OK) {
            s_wifi_password[0] = '\0';
        }

        required_size = sizeof(s_hostname);
        if (nvs_get_str(nvs_handle, NVS_KEY_HOSTNAME, s_hostname, &required_size) != ESP_OK) {
            s_hostname[0] = '\0';
        }

        required_size = sizeof(s_image_url);
        if (nvs_get_str(nvs_handle, NVS_KEY_IMAGE_URL, s_image_url, &required_size) != ESP_OK) {
            s_image_url[0] = '\0';
        }

        uint8_t val_u8;

        if (nvs_get_u8(nvs_handle, NVS_KEY_SWAP_COLORS, &val_u8) == ESP_OK) {
            s_swap_colors = (val_u8 != 0);
        }
        
        if (nvs_get_u8(nvs_handle, NVS_KEY_WIFI_POWER_SAVE, &val_u8) == ESP_OK) {
            s_wifi_power_save = val_u8;
        }

        if (nvs_get_u8(nvs_handle, NVS_KEY_SKIP_VERSION, &val_u8) == ESP_OK) {
            s_skip_display_version = (val_u8 != 0);
        }

        if (nvs_get_u8(nvs_handle, NVS_KEY_AP_MODE, &val_u8) == ESP_OK) {
            s_ap_mode = (val_u8 != 0);
        }

        if (nvs_get_u8(nvs_handle, NVS_KEY_PREFER_IPV6, &val_u8) == ESP_OK) {
            s_prefer_ipv6 = (val_u8 != 0);
        }

        nvs_close(nvs_handle);
    }

    // Check hardcoded defaults if NVS didn't provide credentials
    bool save_defaults = false;
    if (strlen(s_wifi_ssid) == 0) {
        char placeholder_ssid[MAX_SSID_LEN + 1] = WIFI_SSID;
        char placeholder_password[MAX_PASSWORD_LEN + 1] = WIFI_PASSWORD;
        char placeholder_url[MAX_URL_LEN + 1] = REMOTE_URL;

        if (strstr(placeholder_ssid, "Xplaceholder") == NULL && strlen(placeholder_ssid) > 0) {
            strncpy(s_wifi_ssid, placeholder_ssid, MAX_SSID_LEN);
            s_wifi_ssid[MAX_SSID_LEN] = '\0';

            if (strstr(placeholder_password, "Xplaceholder") == NULL) {
                strncpy(s_wifi_password, placeholder_password, MAX_PASSWORD_LEN);
                s_wifi_password[MAX_PASSWORD_LEN] = '\0';
            } else {
                s_wifi_password[0] = '\0';
            }
            save_defaults = true;
        }
        
        // Also check URL
        if (strlen(s_image_url) == 0 && strstr(placeholder_url, "Xplaceholder") == NULL && strlen(placeholder_url) > 0) {
             strncpy(s_image_url, placeholder_url, MAX_URL_LEN);
             s_image_url[MAX_URL_LEN] = '\0';
             // If we have credentials, we save everything. If we don't have credentials but have URL, we might want to save URL too?
             // But existing logic only saved if SSID/PASS were valid.
        }
    }

    if (save_defaults && strlen(s_wifi_ssid) > 0 && strlen(s_wifi_password) > 0) {
        nvs_save_settings();
    }

    ESP_LOGI(TAG, "Settings initialized. SSID: %s, URL: %s, AP Mode: %d", 
             s_wifi_ssid, s_image_url, s_ap_mode);

    return ESP_OK;
}

esp_err_t nvs_get_ssid(char *ssid, size_t max_len) {
    if (!ssid) return ESP_ERR_INVALID_ARG;
    strncpy(ssid, s_wifi_ssid, max_len);
    ssid[max_len - 1] = '\0';
    return ESP_OK;
}

esp_err_t nvs_get_password(char *password, size_t max_len) {
    if (!password) return ESP_ERR_INVALID_ARG;
    strncpy(password, s_wifi_password, max_len);
    password[max_len - 1] = '\0';
    return ESP_OK;
}

esp_err_t nvs_get_hostname(char *hostname, size_t max_len) {
    if (!hostname) return ESP_ERR_INVALID_ARG;
    strncpy(hostname, s_hostname, max_len);
    hostname[max_len - 1] = '\0';
    return ESP_OK;
}

const char* nvs_get_image_url(void) {
    return (strlen(s_image_url) > 0) ? s_image_url : NULL;
}

bool nvs_get_swap_colors(void) {
    return s_swap_colors;
}

wifi_ps_type_t nvs_get_wifi_power_save(void) {
    return s_wifi_power_save;
}

bool nvs_get_skip_display_version(void) {
    return s_skip_display_version;
}

bool nvs_get_ap_mode(void) {
    return s_ap_mode;
}

bool nvs_get_prefer_ipv6(void) {
    return s_prefer_ipv6;
}

esp_err_t nvs_set_ssid(const char *ssid) {
    if (!ssid) return ESP_ERR_INVALID_ARG;
    if (strlen(ssid) > MAX_SSID_LEN) return ESP_ERR_INVALID_SIZE;
    strncpy(s_wifi_ssid, ssid, MAX_SSID_LEN);
    s_wifi_ssid[MAX_SSID_LEN] = '\0';
    return ESP_OK;
}

esp_err_t nvs_set_password(const char *password) {
    if (!password) return ESP_ERR_INVALID_ARG;
    if (strlen(password) > MAX_PASSWORD_LEN) return ESP_ERR_INVALID_SIZE;
    strncpy(s_wifi_password, password, MAX_PASSWORD_LEN);
    s_wifi_password[MAX_PASSWORD_LEN] = '\0';
    return ESP_OK;
}

esp_err_t nvs_set_hostname(const char *hostname) {
    if (!hostname) return ESP_ERR_INVALID_ARG;
    if (strlen(hostname) > MAX_HOSTNAME_LEN) return ESP_ERR_INVALID_SIZE;
    strncpy(s_hostname, hostname, MAX_HOSTNAME_LEN);
    s_hostname[MAX_HOSTNAME_LEN] = '\0';
    return ESP_OK;
}

esp_err_t nvs_set_image_url(const char *image_url) {
    if (!image_url) {
        s_image_url[0] = '\0';
        return ESP_OK;
    }
    if (strlen(image_url) > MAX_URL_LEN) return ESP_ERR_INVALID_SIZE;
    strncpy(s_image_url, image_url, MAX_URL_LEN);
    s_image_url[MAX_URL_LEN] = '\0';
    return ESP_OK;
}

esp_err_t nvs_set_swap_colors(bool swap_colors) {
    s_swap_colors = swap_colors;
    return ESP_OK;
}

esp_err_t nvs_set_wifi_power_save(wifi_ps_type_t power_save) {
    s_wifi_power_save = power_save;
    return ESP_OK;
}

esp_err_t nvs_set_skip_display_version(bool skip) {
    s_skip_display_version = skip;
    return ESP_OK;
}

esp_err_t nvs_set_ap_mode(bool ap_mode) {
    s_ap_mode = ap_mode;
    return ESP_OK;
}

esp_err_t nvs_set_prefer_ipv6(bool prefer_ipv6) {
    s_prefer_ipv6 = prefer_ipv6;
    return ESP_OK;
}

esp_err_t nvs_save_settings(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    if (strlen(s_wifi_ssid) > 0) {
        nvs_set_str(nvs_handle, NVS_KEY_SSID, s_wifi_ssid);
    }
    if (strlen(s_wifi_password) > 0) {
        nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, s_wifi_password);
    }
    if (strlen(s_hostname) > 0) {
        nvs_set_str(nvs_handle, NVS_KEY_HOSTNAME, s_hostname);
    }
    if (strlen(s_image_url) > 0) {
        nvs_set_str(nvs_handle, NVS_KEY_IMAGE_URL, s_image_url);
    }

    nvs_set_u8(nvs_handle, NVS_KEY_SWAP_COLORS, s_swap_colors ? 1 : 0);
    nvs_set_u8(nvs_handle, NVS_KEY_WIFI_POWER_SAVE, (uint8_t)s_wifi_power_save);
    nvs_set_u8(nvs_handle, NVS_KEY_SKIP_VERSION, s_skip_display_version ? 1 : 0);
    nvs_set_u8(nvs_handle, NVS_KEY_AP_MODE, s_ap_mode ? 1 : 0);
    nvs_set_u8(nvs_handle, NVS_KEY_PREFER_IPV6, s_prefer_ipv6 ? 1 : 0);

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err;
}
