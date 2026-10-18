#pragma once

// On-device apps runtime ("local mode"): renders a rotation of built-in apps
// without a server. Configured with a JSON document stored in NVS:
//
// {
//   "timezone": "CET-1CEST,M3.5.0,M10.5.0/3",          POSIX TZ string
//   "brightness": {"day": 60, "night": 10, "night_start": 22, "night_end": 7},
//   "rotation": [
//     {"app": "clock", "dwell": 10, "enabled": true, "config": {"24h": true}},
//     ...
//   ]
// }

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>

// True when the image URL selects local mode (empty or "local").
bool apps_local_mode(const char *image_url);

// Load the stored config. Call once at startup, before the API server.
void apps_init(void);

// Start the rendering task (local mode). Call once WiFi is up.
void apps_start(void);

// Current config as JSON (caller frees).
char *apps_get_config_json(void);
// Validate, store and apply a new config. On error returns ESP_ERR_INVALID_ARG
// and sets *error to a static message.
esp_err_t apps_set_config_json(const char *json, const char **error);

// Show a notification next (interrupts the current app).
esp_err_t apps_notify(const char *title, const char *text, const char *color,
                      int secs);
// Skip to the next app.
void apps_next(void);
// Display on/off from the touch HOLD gesture.
void apps_set_display_power(bool on);
// Name of the app currently on screen ("" if none).
const char *apps_current(void);
// List of built-in app names, as a JSON array (caller frees).
char *apps_list_json(void);
