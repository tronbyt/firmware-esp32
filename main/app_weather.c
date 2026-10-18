// Current weather from Open-Meteo (no API key needed).

#include <esp_log.h>
#include <esp_timer.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_api.h"
#include "http_util.h"

static const char *TAG = "app_weather";

#define REFRESH_US (10 * 60 * 1000000LL)

static const rgb_t YELLOW = {255, 200, 0};
static const rgb_t GRAY = {150, 150, 160};
static const rgb_t BLUE = {60, 140, 255};

static struct {
  bool valid;
  int64_t fetched_us;
  double lat, lon;
  int temp, hi, lo, code;
} s;

static void fetch(const cJSON *cfg) {
  double lat = cfg_double(cfg, "latitude", 0), lon = cfg_double(cfg, "longitude", 0);
  bool same_place = s.valid && lat == s.lat && lon == s.lon;
  if (same_place && esp_timer_get_time() - s.fetched_us < REFRESH_US) return;

  char url[320];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code"
           "&daily=temperature_2m_max,temperature_2m_min"
           "&temperature_unit=%s&timezone=auto&forecast_days=1",
           lat, lon,
           strcmp(cfg_str(cfg, "units", "celsius"), "fahrenheit") == 0
               ? "fahrenheit"
               : "celsius");
  http_response_t resp;
  if (http_request("GET", url, NULL, NULL, 16 * 1024, &resp) != ESP_OK ||
      resp.status != 200) {
    ESP_LOGW(TAG, "fetch failed (status %d), keeping last reading", resp.status);
    http_response_free(&resp);
    return;  // an API hiccup keeps showing the last good reading
  }
  cJSON *root = cJSON_Parse(resp.body);
  http_response_free(&resp);
  const cJSON *cur = cJSON_GetObjectItem(root, "current");
  const cJSON *daily = cJSON_GetObjectItem(root, "daily");
  const cJSON *max = cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "temperature_2m_max"), 0);
  const cJSON *min = cJSON_GetArrayItem(cJSON_GetObjectItem(daily, "temperature_2m_min"), 0);
  if (cfg_has_number(cur, "temperature_2m") && cJSON_IsNumber(max) &&
      cJSON_IsNumber(min)) {
    s.temp = (int)lround(cfg_double(cur, "temperature_2m", 0));
    s.code = cfg_int(cur, "weather_code", 0);
    s.hi = (int)lround(max->valuedouble);
    s.lo = (int)lround(min->valuedouble);
    s.lat = lat;
    s.lon = lon;
    s.valid = true;
    s.fetched_us = esp_timer_get_time();
  } else {
    ESP_LOGW(TAG, "unexpected response");
  }
  cJSON_Delete(root);
}

static bool weather_prepare(const cJSON *cfg) {
  if (cfg_has_number(cfg, "latitude") && cfg_has_number(cfg, "longitude")) {
    fetch(cfg);
  }
  return true;
}

typedef enum { SUN, PARTLY, CLOUD, FOG, RAIN, SNOW, STORM } icon_t;

static const char *condition(int code, icon_t *icon) {
  if (code == 0) return *icon = SUN, "Clear";
  if (code == 1 || code == 2) return *icon = PARTLY, "Partly";
  if (code == 3) return *icon = CLOUD, "Cloudy";
  if (code == 45 || code == 48) return *icon = FOG, "Fog";
  if (code >= 51 && code <= 57) return *icon = RAIN, "Drizzle";
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82))
    return *icon = RAIN, "Rain";
  if ((code >= 71 && code <= 77) || code == 85 || code == 86)
    return *icon = SNOW, "Snow";
  if (code >= 95) return *icon = STORM, "Storm";
  return *icon = CLOUD, "?";
}

static void cloud(int dx, int dy) {
  canvas_ellipse(3 + dx, 11 + dy, 11 + dx, 19 + dy, GRAY);
  canvas_ellipse(7 + dx, 7 + dy, 17 + dx, 17 + dy, GRAY);
  canvas_rect(6 + dx, 15 + dy, 19 + dx, 19 + dy, GRAY);
  canvas_ellipse(14 + dx, 12 + dy, 20 + dx, 19 + dy, GRAY);
}

static void draw_icon(icon_t icon) {
  if (icon == SUN || icon == PARTLY) {
    static const int rays[][4] = {{11, 0, 11, 2},  {11, 17, 11, 19}, {2, 9, 4, 9},
                                  {19, 9, 21, 9},  {4, 2, 5, 3},     {17, 2, 18, 3},
                                  {4, 16, 5, 15},  {18, 16, 17, 15}};
    canvas_ellipse(6, 4, 17, 15, YELLOW);
    for (int i = 0; i < 8; i++)
      canvas_line(rays[i][0], rays[i][1], rays[i][2], rays[i][3], YELLOW);
    if (icon == PARTLY) cloud(1, 6);
  } else if (icon == FOG) {
    for (int y = 8; y <= 20; y += 4) canvas_line(3, y, 19, y, GRAY);
  } else {
    cloud(0, 2);
    if (icon == RAIN) {
      for (int x = 6; x <= 18; x += 4) canvas_line(x, 23, x - 1, 26, BLUE);
    } else if (icon == SNOW) {
      static const int flakes[][2] = {{6, 24}, {10, 26}, {14, 24}, {18, 26}};
      for (int i = 0; i < 4; i++)
        canvas_pixel(flakes[i][0], flakes[i][1], RGB(255, 255, 255));
    } else if (icon == STORM) {
      canvas_line(12, 22, 9, 26, YELLOW);
      canvas_line(9, 26, 13, 26, YELLOW);
      canvas_line(13, 26, 10, 30, YELLOW);
    }
  }
}

static uint32_t weather_draw(const cJSON *cfg, uint32_t t_ms) {
  if (!cfg_has_number(cfg, "latitude") || !cfg_has_number(cfg, "longitude")) {
    return message_draw("Weather", "Set latitude/longitude in the settings",
                        RGB(255, 255, 255), t_ms);
  }
  if (!s.valid) {
    return message_draw("Weather", "No data yet", RGB(150, 150, 150), t_ms);
  }
  icon_t icon;
  const char *label = condition(s.code, &icon);
  draw_icon(icon);

  char buf[16];
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", s.temp);  // degree sign in UTF-8
  canvas_text(&font_6x13, 25, 2, buf, RGB(255, 255, 255));
  canvas_text(&font_tb8, 25, 15, label, RGB(130, 200, 255));
  snprintf(buf, sizeof(buf), "%d", s.hi);
  int w = canvas_text(&font_4x5, 25, 25, buf, RGB(255, 120, 80));
  snprintf(buf, sizeof(buf), "%d", s.lo);
  canvas_text(&font_4x5, 25 + w + 3, 25, buf, RGB(100, 160, 255));
  return 0;
}

const app_t app_weather = {"weather", weather_prepare, weather_draw};
