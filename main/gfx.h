#pragma once

#include <esp_websocket_client.h>
#include <stddef.h>

int gfx_initialize(const char* img_url);
void gfx_set_websocket_handle(esp_websocket_client_handle_t ws_handle);
int gfx_update(void* webp, size_t len, int32_t dwell_secs);
int gfx_get_loaded_counter(void);
bool gfx_has_queued_image(void);
#ifdef CONFIG_BOARD_TIDBYT_GEN2
void gfx_set_pending_sound(const char* sound, uint8_t volume);
#endif
int gfx_display_asset(const char* asset_type);
void gfx_display_text(const char* text, int x, int y, uint8_t r, uint8_t g,
                      uint8_t b, int scale);
void gfx_stop(void);
void gfx_start(void);
void gfx_shutdown(void);
