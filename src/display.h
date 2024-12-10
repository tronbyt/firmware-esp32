#pragma once

#include <stddef.h>
#include <stdint.h>

#define DISPLAY_MAX_BRIGHTNESS 100
#define DISPLAY_MIN_BRIGHTNESS 1
#define DISPLAY_DEFAULT_BRIGHTNESS 30

#ifdef __cplusplus
extern "C" {
#endif

int display_initialize();
void display_set_brightness(int b);
void display_shutdown();

void display_draw(const uint8_t *pix, int width, int height, int channels,
                  int ixR, int ixG, int ixB);

void display_clear();

#ifdef __cplusplus
}
#endif
