// Live departures from Transitous (MOTIS, open DELFI data incl. HVV realtime).
//
// cfg: stop_id (station), platform_ids (only these platforms), optional lines
// and exclude_headsigns filters, rename {headsign: short name}, rows,
// min_minutes, title.

#include <esp_log.h>
#include <esp_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_api.h"
#include "http_util.h"

static const char *TAG = "app_departures";

#define REFRESH_US (30 * 1000000LL)
#define MAX_ROWS 8

typedef struct {
  char line[8];
  char to[48];
  time_t dep;    // realtime departure (or scheduled if no realtime)
  time_t sched;  // timetable departure
  int delay_min;
  bool realtime;
  bool cancelled;
  rgb_t color;
} row_t;

static struct {
  row_t rows[MAX_ROWS];
  int count;
  bool fetched;
  int64_t fetched_us;
  char stop_id[64];
} s;

static bool in_string_array(const cJSON *arr, const char *value) {
  const cJSON *item;
  cJSON_ArrayForEach(item, arr) {
    if (cJSON_IsString(item) && strcmp(item->valuestring, value) == 0)
      return true;
  }
  return false;
}

static int cmp_dep(const void *a, const void *b) {
  time_t da = ((const row_t *)a)->dep, db = ((const row_t *)b)->dep;
  return (da > db) - (da < db);
}

static void fetch(const cJSON *cfg) {
  const char *stop_id = cfg_str(cfg, "stop_id", "");
  if (s.fetched && strcmp(stop_id, s.stop_id) == 0 &&
      esp_timer_get_time() - s.fetched_us < REFRESH_US)
    return;

  char url[200];
  snprintf(url, sizeof(url),
           "https://api.transitous.org/api/v5/stoptimes?stopId=%s&n=%d",
           stop_id, cfg_int(cfg, "fetch", 20));
  http_response_t resp;
  if (http_request("GET", url, NULL, NULL, 256 * 1024, &resp) != ESP_OK ||
      resp.status != 200) {
    ESP_LOGW(TAG, "fetch failed (status %d)", resp.status);
    http_response_free(&resp);
    return;
  }
  cJSON *root = cJSON_Parse(resp.body);
  http_response_free(&resp);
  const cJSON *stop_times = cJSON_GetObjectItem(root, "stopTimes");
  if (!cJSON_IsArray(stop_times)) {
    ESP_LOGW(TAG, "unexpected response");
    cJSON_Delete(root);
    return;
  }

  const cJSON *platforms = cJSON_GetObjectItem(cfg, "platform_ids");
  const cJSON *lines = cJSON_GetObjectItem(cfg, "lines");
  const cJSON *excluded = cJSON_GetObjectItem(cfg, "exclude_headsigns");
  int count = 0;
  const cJSON *st;
  cJSON_ArrayForEach(st, stop_times) {
    if (count == MAX_ROWS) break;
    const cJSON *place = cJSON_GetObjectItem(st, "place");
    const char *platform = cfg_str(place, "stopId", "");
    const char *line = cfg_str(st, "routeShortName", "?");
    const char *headsign = cfg_str(st, "headsign", "");
    if (cJSON_GetArraySize(platforms) > 0 && !in_string_array(platforms, platform))
      continue;
    if (cJSON_GetArraySize(lines) > 0 && !in_string_array(lines, line)) continue;
    if (in_string_array(excluded, headsign)) continue;

    time_t dep = parse_iso8601_utc(cfg_str(place, "departure", ""));
    time_t sched = parse_iso8601_utc(cfg_str(place, "scheduledDeparture", ""));
    if (!dep) dep = sched;
    if (!dep) continue;

    // The DELFI feed sometimes lists the same trip under two platform IDs.
    bool dup = false;
    for (int i = 0; i < count; i++) {
      if (strcmp(s.rows[i].line, line) == 0 && strcmp(s.rows[i].to, headsign) == 0 &&
          s.rows[i].sched == sched) {
        dup = true;
        break;
      }
    }
    if (dup) continue;

    row_t *r = &s.rows[count++];
    snprintf(r->line, sizeof(r->line), "%s", line);
    snprintf(r->to, sizeof(r->to), "%s", headsign);
    r->dep = dep;
    r->sched = sched;
    r->delay_min = sched ? (int)((dep - sched) / 60) : 0;
    r->realtime = cfg_bool(st, "realTime", false);
    r->cancelled = cfg_bool(st, "cancelled", false) || cfg_bool(st, "tripCancelled", false);
    r->color = parse_color(cfg_str(st, "routeColor", NULL), RGB(200, 30, 40));
  }
  cJSON_Delete(root);

  qsort(s.rows, count, sizeof(row_t), cmp_dep);
  s.count = count;
  s.fetched = true;
  s.fetched_us = esp_timer_get_time();
  snprintf(s.stop_id, sizeof(s.stop_id), "%s", stop_id);
  ESP_LOGI(TAG, "%d departures for %s", count, stop_id);
}

static bool departures_prepare(const cJSON *cfg) {
  if (cfg_str(cfg, "stop_id", "")[0] && time_is_valid()) fetch(cfg);
  return true;
}

static uint32_t departures_draw(const cJSON *cfg, uint32_t t_ms) {
  const char *title = cfg_str(cfg, "title", "Bus");
  if (!cfg_str(cfg, "stop_id", "")[0]) {
    return message_draw(title, "Set stop_id in the settings", RGB(255, 255, 255), t_ms);
  }
  time_t now = time(NULL);
  int min_minutes = cfg_int(cfg, "min_minutes", 0);
  int max_rows = cfg_int(cfg, "rows", 3);
  const cJSON *rename = cJSON_GetObjectItem(cfg, "rename");

  int shown = 0;
  for (int i = 0; i < s.count && shown < max_rows; i++) {
    const row_t *r = &s.rows[i];
    int minutes = (int)((r->dep - now) / 60);
    if (r->dep < now) minutes = -1;  // already gone
    if (minutes < min_minutes) continue;
    int y = 1 + shown++ * 11;

    // line badge in the route's colour
    canvas_rect(0, y, 14, y + 8, r->color);
    int lw = canvas_text_width(&font_4x5, r->line);
    canvas_text(&font_4x5, (15 - lw) / 2 + 1, y + 2, r->line, RGB(255, 255, 255));

    // minutes, right-aligned: green = live & on time, orange = late,
    // white = timetable only
    char when[16];
    rgb_t color;
    if (r->cancelled) {
      snprintf(when, sizeof(when), "X");
      color = RGB(255, 60, 60);
    } else {
      if (minutes <= 0) {
        snprintf(when, sizeof(when), "now");
      } else {
        snprintf(when, sizeof(when), "%d", minutes);
      }
      color = !r->realtime        ? RGB(255, 255, 255)
              : r->delay_min >= 2 ? RGB(255, 140, 0)
                                  : RGB(80, 220, 100);
    }
    int ww = canvas_text_width(&font_tb8, when);
    canvas_text(&font_tb8, CANVAS_W - ww, y + 1, when, color);

    // destination, cut to the space between badge and minutes
    const char *to = cfg_str(rename, r->to, r->to);
    char fitted[48];
    canvas_text_fit(&font_tb8, to, CANVAS_W - ww - 19, fitted, sizeof(fitted));
    canvas_text(&font_tb8, 17, y + 1, fitted,
                r->cancelled ? RGB(120, 120, 120) : RGB(255, 255, 255));
  }
  if (shown == 0) {
    canvas_text_centered(&font_tb8, 8, title, RGB(255, 200, 0));
    canvas_text_centered(&font_tb8, 19, s.fetched ? "no departures" : "loading",
                         RGB(120, 120, 120));
  }
  return 0;
}

const app_t app_departures = {"departures", departures_prepare, departures_draw};
