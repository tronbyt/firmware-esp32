#include <time.h>

#include "app_api.h"

static bool clock_prepare(const cJSON *cfg) { return true; }

static uint32_t clock_draw(const cJSON *cfg, uint32_t t_ms) {
  if (!time_is_valid()) {
    canvas_text_centered(&font_10x20, 4, "--:--", RGB(255, 255, 255));
    canvas_text_centered(&font_tb8, 23, "syncing time", RGB(120, 120, 120));
    return 0;
  }
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);

  char hhmm[8], date[24];
  strftime(hhmm, sizeof(hhmm), cfg_bool(cfg, "24h", true) ? "%H:%M" : "%I:%M",
           &tm);
  if ((t_ms / 500) % 2) hhmm[2] = ' ';  // blink the colon
  strftime(date, sizeof(date), "%a %d %b", &tm);

  canvas_text_centered(&font_10x20, 4, hhmm, RGB(255, 255, 255));
  canvas_text_centered(&font_tb8, 23, date, RGB(255, 140, 60));
  return 0;
}

const app_t app_clock = {"clock", clock_prepare, clock_draw};
