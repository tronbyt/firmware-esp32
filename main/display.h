#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

#define DISPLAY_MAX_BRIGHTNESS 100
#define DISPLAY_MIN_BRIGHTNESS 0

extern volatile int32_t isAnimating;  // Declare the variable
#ifdef __cplusplus
extern "C" {
#endif
int display_initialize(void);
void display_set_brightness(uint8_t brightness_pct);
void display_shutdown(void);

void display_draw(const uint8_t* pix, int width, int height);
void display_draw_buffer(const uint8_t* pix, int width, int height);
void display_clear(void);
void display_draw_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void display_fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g,
                       uint8_t b);
void draw_error_indicator_pixel(void);
void display_text(const char* text, int x, int y, uint8_t r, uint8_t g,
                  uint8_t b, int scale);
void display_flip(void);
bool display_wait_frame(uint32_t timeout_ms);
// int32_t isAnimating = 0;  // Initialize with a valid value

#ifdef __cplusplus
}
#endif
