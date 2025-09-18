#pragma once

#include <stddef.h>
#include <stdint.h>

#define DISPLAY_MAX_BRIGHTNESS 100
#define DISPLAY_MIN_BRIGHTNESS 1
#define DISPLAY_DEFAULT_BRIGHTNESS 30
extern int32_t isAnimating;  // Declare the variable
#ifdef __cplusplus
extern "C" {
#endif
int display_initialize();
void display_set_brightness(uint8_t brightness_pct);
uint8_t display_get_brightness();
void display_shutdown();

void display_draw(const uint8_t *pix, int width, int height, int channels,
                  int ixR, int ixG, int ixB);

void display_clear();
void display_draw_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void draw_error_indicator_pixel();
// int32_t isAnimating = 0;  // Initialize with a valid value

#ifdef __cplusplus
}
#endif
