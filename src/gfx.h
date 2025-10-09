#pragma once

#include <stddef.h>
#include <esp_websocket_client.h>

int gfx_initialize();
void gfx_set_websocket_handle(esp_websocket_client_handle_t ws_handle);
int gfx_update(void* webp, size_t len, int32_t dwell_secs);
int gfx_get_loaded_counter();
int gfx_display_asset(const char* asset_type);
void gfx_shutdown();
