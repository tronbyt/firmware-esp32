#pragma once

#include <esp_event.h>
#include <esp_websocket_client.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//------------------------------------------------------------------------------
// Event Base and IDs
//------------------------------------------------------------------------------

ESP_EVENT_DECLARE_BASE(GFX_PLAYER_EVENTS);
enum {
  GFX_PLAYER_EVT_PLAYING,  // playback started (payload: gfx_playing_evt_t)
  GFX_PLAYER_EVT_ERROR,    // decode failed after retries (payload: gfx_error_evt_t)
  GFX_PLAYER_EVT_STOPPED,  // playback ended naturally (no payload)
};

//------------------------------------------------------------------------------
// Source Type - RAM buffer vs Embedded sprite
//------------------------------------------------------------------------------

typedef enum {
  GFX_SOURCE_RAM,       // Dynamic WebP from HTTP/WS (SPIRAM, freed by player)
  GFX_SOURCE_EMBEDDED,  // Static sprite from flash (direct pointer, not freed)
} gfx_source_type_t;

//------------------------------------------------------------------------------
// Event Payload: PLAYING
//------------------------------------------------------------------------------

typedef struct {
  gfx_source_type_t source_type;
  const char* embedded_name;  // Valid if source_type == GFX_SOURCE_EMBEDDED
  uint32_t duration_ms;       // 0 if unlimited
  uint32_t frame_count;       // Number of frames (1 = static image)
} gfx_playing_evt_t;

//------------------------------------------------------------------------------
// Event Payload: ERROR
//------------------------------------------------------------------------------

typedef struct {
  gfx_source_type_t source_type;
  const char* embedded_name;
  int error_code;
} gfx_error_evt_t;

//------------------------------------------------------------------------------
// Lifecycle
//------------------------------------------------------------------------------

int gfx_initialize(const char* img_url);
void gfx_set_websocket_handle(esp_websocket_client_handle_t ws_handle);
void gfx_shutdown(void);

//------------------------------------------------------------------------------
// Playback Control
//------------------------------------------------------------------------------

/**
 * Queue a RAM WebP buffer for playback.
 * Ownership of @p webp transfers to the player only on success.
 * On error (return < 0), caller retains ownership and must free it.
 * @return counter value, or -1 on error
 */
int gfx_update(void* webp, size_t len, int32_t dwell_secs);

/**
 * Play an embedded sprite from flash.
 * Uses direct pointer (no copy). Loops forever until stopped or replaced.
 * @param name  Sprite name: "boot", "config", "error_404", "no_connect",
 *              "oversize"
 * @param immediate  If true, interrupts current playback
 * @return 0 on success, 1 if sprite not found
 */
int gfx_play_embedded(const char* name, bool immediate);

/** @deprecated Use gfx_play_embedded() instead */
int gfx_display_asset(const char* asset_type);

void gfx_display_text(const char* text, int x, int y, uint8_t r, uint8_t g,
                      uint8_t b, int scale);

/** Stop playback and go idle. */
void gfx_stop(void);

/** Resume from stopped state. */
void gfx_start(void);

/** Interrupt current animation to load new content immediately. */
void gfx_interrupt(void);

/** Block until the gfx task finishes the current animation. */
void gfx_wait_idle(void);

//------------------------------------------------------------------------------
// Status Query
//------------------------------------------------------------------------------

/** Returns true if gfx is currently playing an animation. */
bool gfx_is_animating(void);

int gfx_get_loaded_counter(void);

#ifdef __cplusplus
}
#endif
