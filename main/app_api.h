#pragma once

// Interface between the rotation runtime (apps.c) and the individual apps.

#include <cJSON.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "canvas.h"

typedef struct {
  const char *name;  // matches "app" in the rotation config
  // Called when the app's turn comes, before its first frame. Fetch data here
  // (blocking is fine; the previous frame stays on screen). Return false to
  // skip the app this round, e.g. Spotify when nothing is playing.
  bool (*prepare)(const cJSON *cfg);
  // Draw one frame into the canvas, t_ms after the app came on screen.
  // Returns how long the content needs to be shown in full (e.g. one marquee
  // pass), or 0; the runtime shows the app for max(dwell, that).
  uint32_t (*draw)(const cJSON *cfg, uint32_t t_ms);
} app_t;

extern const app_t app_clock;
extern const app_t app_weather;
extern const app_t app_departures;
extern const app_t app_spotify;
extern const app_t app_message;

// Draw a title bar + scrolling message; shared by the message app and
// notifications pushed through the API.
uint32_t message_draw(const char *title, const char *text, rgb_t color,
                      uint32_t t_ms);

// --- config helpers (cfg may be NULL) ---
int cfg_int(const cJSON *cfg, const char *key, int def);
double cfg_double(const cJSON *cfg, const char *key, double def);
bool cfg_bool(const cJSON *cfg, const char *key, bool def);
const char *cfg_str(const cJSON *cfg, const char *key, const char *def);
bool cfg_has_number(const cJSON *cfg, const char *key);
// Parse "#rrggbb" / "rrggbb"; returns def if missing or invalid.
rgb_t parse_color(const char *hex, rgb_t def);

// "2026-10-11T17:46:00Z" -> time_t, or 0 if unparsable.
time_t parse_iso8601_utc(const char *s);
// True once SNTP has set the clock.
bool time_is_valid(void);

// Spotify credentials (stored in NVS by the API).
void spotify_set_credentials(const char *client_id, const char *refresh_token);
bool spotify_is_configured(void);
