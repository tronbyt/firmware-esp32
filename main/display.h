#pragma once

#include <stddef.h>
#include <stdint.h>

// Forward declaration for C++ class (only visible in C++ files)
#ifdef __cplusplus
class MatrixPanel_I2S_DMA;
#else
// For C files, we just use void* to represent the matrix pointer
typedef void MatrixPanel_I2S_DMA;
#endif

// Define display dimensions
#ifndef WIDTH
#define WIDTH 64
#endif
#ifndef HEIGHT
#define HEIGHT 32
#endif

// Display brightness levels
#define DISPLAY_MAX_BRIGHTNESS 100
#define DISPLAY_MIN_BRIGHTNESS 0
#define DISPLAY_DEFAULT_BRIGHTNESS 30

// Extern declaration for animation flag (defined in main.c)
extern volatile int32_t isAnimating;

#ifdef __cplusplus
extern "C" {
#endif

int display_initialize(void);
void display_set_brightness(uint8_t brightness_pct);
void display_shutdown(void);

void display_draw(const uint8_t* pix, int width, int height, int channels,
                  int ixR, int ixG, int ixB);

void display_clear(void);
void display_draw_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void display_fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g,
                       uint8_t b);
void draw_error_indicator_pixel(void);
void display_text(const char* text, int x, int y, uint8_t r, uint8_t g,
                  uint8_t b, int scale);
void display_flip(void);
MatrixPanel_I2S_DMA* display_get_matrix(void);

#ifdef __cplusplus
}
#endif
