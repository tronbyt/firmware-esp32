#pragma once

#include <esp_websocket_client.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gfx_initialize(const char* img_url);
void gfx_set_websocket_handle(esp_websocket_client_handle_t ws_handle);
int gfx_update(void* webp, size_t len, int32_t dwell_secs);
int gfx_get_loaded_counter(void);
int gfx_display_asset(const char* asset_type);
void gfx_display_text(const char* text, int x, int y, uint8_t r, uint8_t g,
                      uint8_t b, int scale);
void gfx_stop(void);
void gfx_start(void);
void gfx_shutdown(void);

/** Interrupt current animation to load new content immediately. */
void gfx_interrupt(void);

/** Block until the gfx task finishes the current animation. */
void gfx_wait_idle(void);

/** Returns true if gfx is currently playing an animation. */
bool gfx_is_animating(void);

#ifdef __cplusplus
}
#endif
