#pragma once

// HTTP API + settings page on port 8080 (the AP setup portal owns port 80).
//
//   GET  /                 settings page
//   GET  /api/status       firmware, mode, network, current app
//   GET  /api/apps         names of the built-in apps
//   GET  /api/config       rotation/settings JSON      PUT replaces it
//   POST /api/notify       {"text", "title", "color", "secs"}
//   POST /api/next         skip to the next app
//   POST /api/spotify      {"client_id", "refresh_token"}   DELETE clears
//   POST /api/mode         {"image_url": "local" | "http://..."}, reboots
//   POST /api/ota          raw firmware .bin body, reboots into it
//   POST /api/reboot

#include <esp_err.h>

esp_err_t api_server_start(void);
