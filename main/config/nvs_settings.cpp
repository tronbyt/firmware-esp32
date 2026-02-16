#include "nvs_settings.h"

#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs_handle.h"
#include "sdkconfig.h"

namespace {

const char* TAG = "NVS_SETTINGS";

constexpr const char* NVS_NAMESPACE = "wifi_config";
constexpr const char* NVS_KEY_SSID = "ssid";
constexpr const char* NVS_KEY_PASSWORD = "password";
constexpr const char* NVS_KEY_HOSTNAME = "hostname";
constexpr const char* NVS_KEY_SYSLOG_ADDR = "syslog_addr";
constexpr const char* NVS_KEY_SNTP_SERVER = "sntp_server";
constexpr const char* NVS_KEY_IMAGE_URL = "image_url";
constexpr const char* NVS_KEY_SWAP_COLORS = "swap_colors";
constexpr const char* NVS_KEY_WIFI_POWER_SAVE = "wifi_ps";
constexpr const char* NVS_KEY_SKIP_VERSION = "skip_ver";
constexpr const char* NVS_KEY_AP_MODE = "ap_mode";
constexpr const char* NVS_KEY_PREFER_IPV6 = "prefer_ipv6";

system_config_t s_config = {};
SemaphoreHandle_t s_mutex = nullptr;

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef REMOTE_URL
#define REMOTE_URL ""
#endif

/// Write all fields of s_config to NVS. Caller must hold s_mutex.
esp_err_t persist_to_nvs() {
  NvsHandle nvs(NVS_NAMESPACE, NVS_READWRITE);
  if (!nvs) return nvs.open_error();

  nvs.set_str(NVS_KEY_SSID, s_config.ssid);
  nvs.set_str(NVS_KEY_PASSWORD, s_config.password);
  nvs.set_str(NVS_KEY_HOSTNAME, s_config.hostname);
  nvs.set_str(NVS_KEY_SYSLOG_ADDR, s_config.syslog_addr);
  nvs.set_str(NVS_KEY_SNTP_SERVER, s_config.sntp_server);
  nvs.set_str(NVS_KEY_IMAGE_URL, s_config.image_url);

  nvs.set_u8(NVS_KEY_SWAP_COLORS, s_config.swap_colors ? 1 : 0);
  nvs.set_u8(NVS_KEY_WIFI_POWER_SAVE,
             static_cast<uint8_t>(s_config.wifi_power_save));
  nvs.set_u8(NVS_KEY_SKIP_VERSION, s_config.skip_display_version ? 1 : 0);
  nvs.set_u8(NVS_KEY_AP_MODE, s_config.ap_mode ? 1 : 0);
  nvs.set_u8(NVS_KEY_PREFER_IPV6, s_config.prefer_ipv6 ? 1 : 0);

  return nvs.commit();
}

}  // namespace

esp_err_t nvs_settings_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  if (!s_mutex) return ESP_ERR_NO_MEM;

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) return ret;

  memset(&s_config, 0, sizeof(s_config));

  // Kconfig defaults
#ifdef CONFIG_SWAP_COLORS
  s_config.swap_colors = true;
#endif

#ifdef CONFIG_ENABLE_WIFI_POWER_SAVE
  s_config.wifi_power_save = WIFI_PS_MIN_MODEM;
#else
  s_config.wifi_power_save = WIFI_PS_NONE;
#endif

#ifdef CONFIG_SKIP_DISPLAY_VERSION
  s_config.skip_display_version = true;
#endif

#ifdef CONFIG_ENABLE_AP_MODE
  s_config.ap_mode = true;
#endif

#ifdef CONFIG_PREFER_IPV6
  s_config.prefer_ipv6 = true;
#endif

  // Load from NVS (overrides Kconfig defaults)
  {
    NvsHandle nvs(NVS_NAMESPACE, NVS_READONLY);
    if (nvs) {
      size_t sz;

      sz = sizeof(s_config.ssid);
      if (nvs.get_str(NVS_KEY_SSID, s_config.ssid, &sz) != ESP_OK)
        s_config.ssid[0] = '\0';

      sz = sizeof(s_config.password);
      if (nvs.get_str(NVS_KEY_PASSWORD, s_config.password, &sz) != ESP_OK)
        s_config.password[0] = '\0';

      sz = sizeof(s_config.hostname);
      if (nvs.get_str(NVS_KEY_HOSTNAME, s_config.hostname, &sz) != ESP_OK)
        s_config.hostname[0] = '\0';

      sz = sizeof(s_config.syslog_addr);
      if (nvs.get_str(NVS_KEY_SYSLOG_ADDR, s_config.syslog_addr, &sz) !=
          ESP_OK)
        s_config.syslog_addr[0] = '\0';

      sz = sizeof(s_config.sntp_server);
      if (nvs.get_str(NVS_KEY_SNTP_SERVER, s_config.sntp_server, &sz) !=
          ESP_OK)
        s_config.sntp_server[0] = '\0';

      sz = sizeof(s_config.image_url);
      if (nvs.get_str(NVS_KEY_IMAGE_URL, s_config.image_url, &sz) != ESP_OK)
        s_config.image_url[0] = '\0';

      uint8_t val_u8;

      if (nvs.get_u8(NVS_KEY_SWAP_COLORS, &val_u8) == ESP_OK)
        s_config.swap_colors = (val_u8 != 0);

      if (nvs.get_u8(NVS_KEY_WIFI_POWER_SAVE, &val_u8) == ESP_OK)
        s_config.wifi_power_save = static_cast<wifi_ps_type_t>(val_u8);

      if (nvs.get_u8(NVS_KEY_SKIP_VERSION, &val_u8) == ESP_OK)
        s_config.skip_display_version = (val_u8 != 0);

      if (nvs.get_u8(NVS_KEY_AP_MODE, &val_u8) == ESP_OK)
        s_config.ap_mode = (val_u8 != 0);

      if (nvs.get_u8(NVS_KEY_PREFER_IPV6, &val_u8) == ESP_OK)
        s_config.prefer_ipv6 = (val_u8 != 0);
    }
  }

  // Apply secrets.json defaults if NVS has no SSID
  bool save_defaults = false;
  if (strlen(s_config.ssid) == 0) {
    char placeholder_ssid[MAX_SSID_LEN + 1] = WIFI_SSID;
    char placeholder_password[MAX_PASSWORD_LEN + 1] = WIFI_PASSWORD;
    char placeholder_url[MAX_URL_LEN + 1] = REMOTE_URL;

    if (strstr(placeholder_ssid, "Xplaceholder") == nullptr &&
        strlen(placeholder_ssid) > 0) {
      snprintf(s_config.ssid, sizeof(s_config.ssid), "%s", placeholder_ssid);

      if (strstr(placeholder_password, "Xplaceholder") == nullptr) {
        snprintf(s_config.password, sizeof(s_config.password), "%s",
                 placeholder_password);
      } else {
        s_config.password[0] = '\0';
      }
      save_defaults = true;
    }

    if (strlen(s_config.image_url) == 0 &&
        strstr(placeholder_url, "Xplaceholder") == nullptr &&
        strlen(placeholder_url) > 0) {
      snprintf(s_config.image_url, sizeof(s_config.image_url), "%s",
               placeholder_url);
    }
  }

  if (save_defaults && strlen(s_config.ssid) > 0 &&
      strlen(s_config.password) > 0) {
    persist_to_nvs();
  }

  ESP_LOGI(TAG, "Settings initialized. SSID: %s, URL: %s, AP Mode: %d",
           s_config.ssid, s_config.image_url, s_config.ap_mode);

  return ESP_OK;
}

system_config_t config_get(void) {
  system_config_t copy;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  memcpy(&copy, &s_config, sizeof(system_config_t));
  xSemaphoreGive(s_mutex);
  return copy;
}

void config_set(const system_config_t* cfg) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  memcpy(&s_config, cfg, sizeof(system_config_t));
  persist_to_nvs();
  xSemaphoreGive(s_mutex);
}
