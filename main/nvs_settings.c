#include "nvs_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi_types.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define TAG "NVS_SETTINGS"

// NVS namespace and keys
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"
#define NVS_KEY_HOSTNAME "hostname"
#define NVS_KEY_SYSLOG_ADDR "syslog_addr"
#define NVS_KEY_SNTP_SERVER "sntp_server"
#define NVS_KEY_IMAGE_URL "image_url"
#define NVS_KEY_SWAP_COLORS "swap_colors"
#define NVS_KEY_WIFI_POWER_SAVE "wifi_ps"
#define NVS_KEY_SKIP_VERSION "skip_ver"
#define NVS_KEY_AP_MODE "ap_mode"
#define NVS_KEY_PREFER_IPV6 "prefer_ipv6"
#define NVS_KEY_API_KEY "api_key"
// Double pendulum settings
#define NVS_KEY_PENDULUM_SPEED "pend_speed"
#define NVS_KEY_PENDULUM_ARM1_LENGTH "pend_arm1"
#define NVS_KEY_PENDULUM_ARM2_LENGTH "pend_arm2"
#define NVS_KEY_PENDULUM_MASS1 "pend_mass1"
#define NVS_KEY_PENDULUM_MASS2 "pend_mass2"
#define NVS_KEY_TRAIL_LENGTH "trail_len"
#define NVS_KEY_TRAIL_COLOR_CYCLE "trail_col"
#define NVS_KEY_ARM_EVOLUTION "arm_evo"
#define NVS_KEY_ARM_EVO_SPEED "arm_evo_spd"
#define NVS_KEY_RANDOMIZE_BOOT "rand_boot"
#define NVS_KEY_BRIGHTNESS "brightness"
#define NVS_KEY_LEG_COLOR "leg_color"

// Internal storage
static char s_wifi_ssid[MAX_SSID_LEN + 1] = {0};
static char s_wifi_password[MAX_PASSWORD_LEN + 1] = {0};
static char s_hostname[MAX_HOSTNAME_LEN + 1] = {0};
static char s_syslog_addr[MAX_SYSLOG_ADDR_LEN + 1] = {0};
static char s_sntp_server[MAX_SNTP_SERVER_LEN + 1] = {0};
static char s_image_url[MAX_URL_LEN + 1] = {0};
static bool s_swap_colors = false;
static wifi_ps_type_t s_wifi_power_save = WIFI_PS_MIN_MODEM;
static bool s_skip_display_version = false;
static bool s_ap_mode = true;
static bool s_prefer_ipv6 = false;
static char s_api_key[MAX_API_KEY_LEN + 1] = {0};

// Double pendulum settings defaults - hardcoded values
#define PENDULUM_SPEED 0.01f        // Default physics step size
#define PENDULUM_ARM1_LENGTH 12.0f  // Default arm 1 length in pixels
#define PENDULUM_ARM2_LENGTH 10.0f  // Default arm 2 length in pixels
#define PENDULUM_MASS1 1.0f         // Default mass 1
#define PENDULUM_MASS2 1.0f         // Default mass 2
#define TRAIL_LENGTH_DEFAULT 200    // Default trail length

// Double pendulum settings
static float s_pendulum_speed = PENDULUM_SPEED;
static float s_pendulum_arm1_length = PENDULUM_ARM1_LENGTH;
static float s_pendulum_arm2_length = PENDULUM_ARM2_LENGTH;
static float s_pendulum_mass1 = PENDULUM_MASS1;
static float s_pendulum_mass2 = PENDULUM_MASS2;
static int s_trail_length = TRAIL_LENGTH_DEFAULT;
static bool s_trail_color_cycle = false;
static bool s_arm_evolution_enabled = true;
static float s_arm_evolution_speed = 0.00002f;
static bool s_randomize_on_boot = false;
static int s_brightness = 128;
static int s_leg_color = 0xFFFFFF;

// Hardcoded defaults (from secrets.json via CMake)

esp_err_t nvs_settings_init(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) return ret;

  nvs_handle_t nvs_handle;
  ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);

  // Set defaults first
#ifdef CONFIG_SWAP_COLORS
  s_swap_colors = true;
#else
  s_swap_colors = false;
#endif

#ifdef CONFIG_ENABLE_WIFI_POWER_SAVE
  s_wifi_power_save = WIFI_PS_MIN_MODEM;
#else
  s_wifi_power_save = WIFI_PS_NONE;
#endif

#ifdef CONFIG_SKIP_DISPLAY_VERSION
  s_skip_display_version = true;
#else
  s_skip_display_version = false;
#endif

#ifdef CONFIG_ENABLE_AP_MODE
  s_ap_mode = true;
#else
  s_ap_mode = false;
#endif

#ifdef CONFIG_PREFER_IPV6
  s_prefer_ipv6 = true;
#else
  s_prefer_ipv6 = false;
#endif

  if (ret == ESP_OK) {
    // Load from NVS
    size_t required_size = sizeof(s_wifi_ssid);
    if (nvs_get_str(nvs_handle, NVS_KEY_SSID, s_wifi_ssid, &required_size) !=
        ESP_OK) {
      s_wifi_ssid[0] = '\0';
    }

    required_size = sizeof(s_wifi_password);
    if (nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, s_wifi_password,
                    &required_size) != ESP_OK) {
      s_wifi_password[0] = '\0';
    }

    required_size = sizeof(s_hostname);
    if (nvs_get_str(nvs_handle, NVS_KEY_HOSTNAME, s_hostname, &required_size) !=
        ESP_OK) {
      s_hostname[0] = '\0';
    }

    required_size = sizeof(s_syslog_addr);
    if (nvs_get_str(nvs_handle, NVS_KEY_SYSLOG_ADDR, s_syslog_addr,
                    &required_size) != ESP_OK) {
      s_syslog_addr[0] = '\0';
    }

    required_size = sizeof(s_sntp_server);
    if (nvs_get_str(nvs_handle, NVS_KEY_SNTP_SERVER, s_sntp_server,
                    &required_size) != ESP_OK) {
      s_sntp_server[0] = '\0';
    }

    // Load pendulum settings
    required_size = sizeof(s_pendulum_speed);
    if (nvs_get_blob(nvs_handle, NVS_KEY_PENDULUM_SPEED, &s_pendulum_speed,
                     &required_size) != ESP_OK) {
      s_pendulum_speed = PENDULUM_SPEED;  // Default
    }

    required_size = sizeof(s_pendulum_arm1_length);
    if (nvs_get_blob(nvs_handle, NVS_KEY_PENDULUM_ARM1_LENGTH,
                     &s_pendulum_arm1_length, &required_size) != ESP_OK) {
      s_pendulum_arm1_length = PENDULUM_ARM1_LENGTH;  // Default
    }

    required_size = sizeof(s_pendulum_arm2_length);
    if (nvs_get_blob(nvs_handle, NVS_KEY_PENDULUM_ARM2_LENGTH,
                     &s_pendulum_arm2_length, &required_size) != ESP_OK) {
      s_pendulum_arm2_length = PENDULUM_ARM2_LENGTH;  // Default
    }

    required_size = sizeof(s_pendulum_mass1);
    if (nvs_get_blob(nvs_handle, NVS_KEY_PENDULUM_MASS1, &s_pendulum_mass1,
                     &required_size) != ESP_OK) {
      s_pendulum_mass1 = PENDULUM_MASS1;  // Default
    }

    required_size = sizeof(s_pendulum_mass2);
    if (nvs_get_blob(nvs_handle, NVS_KEY_PENDULUM_MASS2, &s_pendulum_mass2,
                     &required_size) != ESP_OK) {
      s_pendulum_mass2 = PENDULUM_MASS2;  // Default
    }

    // Read trail length
    int32_t trail_len;
    if (nvs_get_i32(nvs_handle, NVS_KEY_TRAIL_LENGTH, &trail_len) != ESP_OK ||
        trail_len < 10 || trail_len > 10000) {
      s_trail_length = TRAIL_LENGTH_DEFAULT;
    } else {
      s_trail_length = (int)trail_len;
    }

    // Read trail color cycle setting
    uint8_t trail_color_val;
    if (nvs_get_u8(nvs_handle, NVS_KEY_TRAIL_COLOR_CYCLE, &trail_color_val) ==
        ESP_OK) {
      s_trail_color_cycle = (trail_color_val != 0);
    } else {
      s_trail_color_cycle = false;  // Default: don't cycle colors
    }

    // Read arm evolution setting
    uint8_t arm_evo_val;
    if (nvs_get_u8(nvs_handle, NVS_KEY_ARM_EVOLUTION, &arm_evo_val) == ESP_OK) {
      s_arm_evolution_enabled = (arm_evo_val != 0);
    } else {
      s_arm_evolution_enabled = true;  // Default: enabled
    }

    // Read arm evolution speed
    required_size = sizeof(s_arm_evolution_speed);
    if (nvs_get_blob(nvs_handle, NVS_KEY_ARM_EVO_SPEED, &s_arm_evolution_speed,
                 &required_size) != ESP_OK) {
      s_arm_evolution_speed = 0.00002f;  // Default ~38 min cycle
    }

    // Read randomize on boot setting
    uint8_t rand_boot_val;
    if (nvs_get_u8(nvs_handle, NVS_KEY_RANDOMIZE_BOOT, &rand_boot_val) == ESP_OK) {
      s_randomize_on_boot = (rand_boot_val != 0);
    } else {
      s_randomize_on_boot = false;  // Default: off
    }

    // Read brightness
    int32_t brightness_val;
    if (nvs_get_i32(nvs_handle, NVS_KEY_BRIGHTNESS, &brightness_val) ==
            ESP_OK &&
        brightness_val >= 1 && brightness_val <= 255) {
      s_brightness = (int)brightness_val;
    } else {
      s_brightness = 128;  // Default
    }

    // Read leg color
    int32_t leg_color_val;
    if (nvs_get_i32(nvs_handle, NVS_KEY_LEG_COLOR, &leg_color_val) == ESP_OK) {
      s_leg_color = (int)leg_color_val;
    } else {
      s_leg_color = 0xFFFFFF;  // Default white
    }

    required_size = sizeof(s_image_url);
    if (nvs_get_str(nvs_handle, NVS_KEY_IMAGE_URL, s_image_url,
                    &required_size) != ESP_OK) {
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

    required_size = sizeof(s_api_key);
    if (nvs_get_str(nvs_handle, NVS_KEY_API_KEY, s_api_key, &required_size) !=
        ESP_OK) {
      s_api_key[0] = '\0';
    }

    nvs_close(nvs_handle);
  }

  // Check hardcoded defaults if NVS didn't provide credentials
  bool save_defaults = false;
  if (strlen(s_wifi_ssid) == 0) {
    char placeholder_ssid[MAX_SSID_LEN + 1] = WIFI_SSID;
    char placeholder_password[MAX_PASSWORD_LEN + 1] = WIFI_PASSWORD;
    char placeholder_url[MAX_URL_LEN + 1] = REMOTE_URL;

    if (strstr(placeholder_ssid, "Xplaceholder") == NULL &&
        strlen(placeholder_ssid) > 0) {
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
    if (strlen(s_image_url) == 0 &&
        strstr(placeholder_url, "Xplaceholder") == NULL &&
        strlen(placeholder_url) > 0) {
      nvs_set_image_url(placeholder_url);
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

esp_err_t nvs_get_syslog_addr(char *addr, size_t max_len) {
  if (!addr) return ESP_ERR_INVALID_ARG;
  strncpy(addr, s_syslog_addr, max_len);
  addr[max_len - 1] = '\0';
  return ESP_OK;
}

esp_err_t nvs_get_sntp_server(char *server, size_t max_len) {
  if (!server) return ESP_ERR_INVALID_ARG;
  strncpy(server, s_sntp_server, max_len);
  server[max_len - 1] = '\0';
  return ESP_OK;
}

const char *nvs_get_image_url(void) {
  return (strlen(s_image_url) > 0) ? s_image_url : NULL;
}

bool nvs_get_swap_colors(void) { return s_swap_colors; }

wifi_ps_type_t nvs_get_wifi_power_save(void) { return s_wifi_power_save; }

bool nvs_get_skip_display_version(void) { return s_skip_display_version; }

bool nvs_get_ap_mode(void) { return s_ap_mode; }

bool nvs_get_prefer_ipv6(void) { return s_prefer_ipv6; }

esp_err_t nvs_get_api_key(char *api_key, size_t max_len) {
  if (!api_key) return ESP_ERR_INVALID_ARG;
  strncpy(api_key, s_api_key, max_len);
  api_key[max_len - 1] = '\0';
  return ESP_OK;
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

esp_err_t nvs_set_syslog_addr(const char *addr) {
  if (!addr) {
    s_syslog_addr[0] = '\0';
    return ESP_OK;
  }
  if (strlen(addr) > MAX_SYSLOG_ADDR_LEN) return ESP_ERR_INVALID_SIZE;
  strncpy(s_syslog_addr, addr, MAX_SYSLOG_ADDR_LEN);
  s_syslog_addr[MAX_SYSLOG_ADDR_LEN] = '\0';
  return ESP_OK;
}

esp_err_t nvs_set_sntp_server(const char *server) {
  if (!server) return ESP_ERR_INVALID_ARG;
  if (strlen(server) > MAX_SNTP_SERVER_LEN) return ESP_ERR_INVALID_SIZE;
  strncpy(s_sntp_server, server, MAX_SNTP_SERVER_LEN);
  s_sntp_server[MAX_SNTP_SERVER_LEN] = '\0';
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

  char *key_start = strstr(s_image_url, "&key=");
  if (!key_start) {
    key_start = strstr(s_image_url, "?key=");
  }
  if (key_start) {
    char *key_value = key_start + 5;
    char *key_end = strchr(key_value, '&');
    size_t key_len =
        key_end ? (size_t)(key_end - key_value) : strlen(key_value);
    if (key_len > 0 && key_len <= MAX_API_KEY_LEN) {
      strncpy(s_api_key, key_value, key_len);
      s_api_key[key_len] = '\0';
      ESP_LOGI(TAG, "Extracted API key from URL");
    }
    if (key_end) {
      char *question_mark = strchr(s_image_url, '?');
      if (question_mark == key_start) {
        *key_end = '?';
      }
      memmove(key_start, key_end, strlen(key_end) + 1);
    } else {
      if (key_start > s_image_url && *(key_start - 1) == '?') {
        key_start--;
      }
      *key_start = '\0';
    }
  }

  return ESP_OK;
}

esp_err_t nvs_set_api_key(const char *api_key) {
  if (!api_key) {
    s_api_key[0] = '\0';
    return ESP_OK;
  }
  if (strlen(api_key) > MAX_API_KEY_LEN) return ESP_ERR_INVALID_SIZE;
  strncpy(s_api_key, api_key, MAX_API_KEY_LEN);
  s_api_key[MAX_API_KEY_LEN] = '\0';
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

// Double pendulum settings getters
float nvs_get_pendulum_speed(void) { return s_pendulum_speed; }

float nvs_get_pendulum_arm1_length(void) { return s_pendulum_arm1_length; }

float nvs_get_pendulum_arm2_length(void) { return s_pendulum_arm2_length; }

float nvs_get_pendulum_mass1(void) { return s_pendulum_mass1; }

float nvs_get_pendulum_mass2(void) { return s_pendulum_mass2; }

int nvs_get_trail_length(void) { return s_trail_length; }

bool nvs_get_trail_color_cycle(void) { return s_trail_color_cycle; }

bool nvs_get_arm_evolution_enabled(void) { return s_arm_evolution_enabled; }

float nvs_get_arm_evolution_speed(void) { return s_arm_evolution_speed; }

bool nvs_get_randomize_on_boot(void) { return s_randomize_on_boot; }

int nvs_get_brightness(void) { return s_brightness; }

int nvs_get_leg_color(void) { return s_leg_color; }

// Double pendulum settings setters
esp_err_t nvs_set_pendulum_speed(float speed) {
  if (speed <= 0.0f || speed > 1.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  s_pendulum_speed = speed;
  return ESP_OK;
}

esp_err_t nvs_set_pendulum_arm1_length(float length) {
  if (length <= 0.0f || length > 50.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  s_pendulum_arm1_length = length;
  return ESP_OK;
}

esp_err_t nvs_set_pendulum_arm2_length(float length) {
  if (length <= 0.0f || length > 50.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  s_pendulum_arm2_length = length;
  return ESP_OK;
}

esp_err_t nvs_set_pendulum_mass1(float mass) {
  if (mass <= 0.0f || mass > 10.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  s_pendulum_mass1 = mass;
  return ESP_OK;
}

esp_err_t nvs_set_pendulum_mass2(float mass) {
  if (mass <= 0.0f || mass > 10.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  s_pendulum_mass2 = mass;
  return ESP_OK;
}

esp_err_t nvs_set_trail_length(int length) {
  if (length < 10 || length > 10000) {
    return ESP_ERR_INVALID_ARG;
  }
  s_trail_length = length;
  return ESP_OK;
}

esp_err_t nvs_set_trail_color_cycle(bool cycle) {
  s_trail_color_cycle = cycle;
  return ESP_OK;
}

esp_err_t nvs_set_arm_evolution_enabled(bool enabled) {
  s_arm_evolution_enabled = enabled;
  return ESP_OK;
}

esp_err_t nvs_set_arm_evolution_speed(float speed) {
  if (speed <= 0.0f || speed > 0.001f) {
    return ESP_ERR_INVALID_ARG;
  }
  s_arm_evolution_speed = speed;
  return ESP_OK;
}

esp_err_t nvs_set_randomize_on_boot(bool enabled) {
  s_randomize_on_boot = enabled;
  return ESP_OK;
}

esp_err_t nvs_set_brightness(int brightness) {
  if (brightness < 1 || brightness > 255) {
    return ESP_ERR_INVALID_ARG;
  }
  s_brightness = brightness;
  return ESP_OK;
}

esp_err_t nvs_set_leg_color(int color) {
  s_leg_color = color;
  return ESP_OK;
}

static bool s_reload_flag = false;

void nvs_settings_set_reload_flag(void) { s_reload_flag = true; }

bool nvs_settings_get_and_clear_reload_flag(void) {
  if (s_reload_flag) {
    s_reload_flag = false;
    return true;
  }
  return false;
}

// Save all modified settings to NVS
esp_err_t nvs_save_settings(void) {
  nvs_handle_t nvs_handle;
  esp_err_t err;

  err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) return err;

  nvs_set_str(nvs_handle, NVS_KEY_SSID, s_wifi_ssid);
  nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, s_wifi_password);
  nvs_set_str(nvs_handle, NVS_KEY_HOSTNAME, s_hostname);
  nvs_set_str(nvs_handle, NVS_KEY_SYSLOG_ADDR, s_syslog_addr);
  nvs_set_str(nvs_handle, NVS_KEY_SNTP_SERVER, s_sntp_server);
  nvs_set_str(nvs_handle, NVS_KEY_IMAGE_URL, s_image_url);
  nvs_set_str(nvs_handle, NVS_KEY_API_KEY, s_api_key);
  nvs_set_blob(nvs_handle, NVS_KEY_PENDULUM_SPEED, &s_pendulum_speed,
               sizeof(s_pendulum_speed));
  nvs_set_blob(nvs_handle, NVS_KEY_PENDULUM_ARM1_LENGTH,
               &s_pendulum_arm1_length, sizeof(s_pendulum_arm1_length));
  nvs_set_blob(nvs_handle, NVS_KEY_PENDULUM_ARM2_LENGTH,
               &s_pendulum_arm2_length, sizeof(s_pendulum_arm2_length));
  nvs_set_blob(nvs_handle, NVS_KEY_PENDULUM_MASS1, &s_pendulum_mass1,
               sizeof(s_pendulum_mass1));
  nvs_set_blob(nvs_handle, NVS_KEY_PENDULUM_MASS2, &s_pendulum_mass2,
               sizeof(s_pendulum_mass2));
  nvs_set_i32(nvs_handle, NVS_KEY_TRAIL_LENGTH, s_trail_length);
nvs_set_u8(nvs_handle, NVS_KEY_TRAIL_COLOR_CYCLE,
              s_trail_color_cycle ? 1 : 0);
  nvs_set_u8(nvs_handle, NVS_KEY_ARM_EVOLUTION,
              s_arm_evolution_enabled ? 1 : 0);
  nvs_set_blob(nvs_handle, NVS_KEY_ARM_EVO_SPEED, &s_arm_evolution_speed,
              sizeof(s_arm_evolution_speed));
  nvs_set_u8(nvs_handle, NVS_KEY_RANDOMIZE_BOOT,
             s_randomize_on_boot ? 1 : 0);
  nvs_set_i32(nvs_handle, NVS_KEY_BRIGHTNESS, s_brightness);
  nvs_set_i32(nvs_handle, NVS_KEY_LEG_COLOR, s_leg_color);

  nvs_set_u8(nvs_handle, NVS_KEY_SWAP_COLORS, s_swap_colors ? 1 : 0);
  nvs_set_u8(nvs_handle, NVS_KEY_WIFI_POWER_SAVE, (uint8_t)s_wifi_power_save);
  nvs_set_u8(nvs_handle, NVS_KEY_SKIP_VERSION, s_skip_display_version ? 1 : 0);
  nvs_set_u8(nvs_handle, NVS_KEY_AP_MODE, s_ap_mode ? 1 : 0);
  nvs_set_u8(nvs_handle, NVS_KEY_PREFER_IPV6, s_prefer_ipv6 ? 1 : 0);

  err = nvs_commit(nvs_handle);
  nvs_close(nvs_handle);
  return err;
}
