#pragma once

// 64x32 RGB framebuffer used by the on-device apps. Coordinates and fill rules
// mirror Pillow's ImageDraw so apps look like their tidbyt-server counterparts.

#include <stdbool.h>
#include <stdint.h>

#include "fonts.h"

#define CANVAS_W 64
#define CANVAS_H 32

typedef struct {
  uint8_t r, g, b;
} rgb_t;

#define RGB(r, g, b) ((rgb_t){(r), (g), (b)})

void canvas_clear(rgb_t color);
void canvas_pixel(int x, int y, rgb_t color);
// Filled rectangle, corners inclusive (like ImageDraw.rectangle).
void canvas_rect(int x0, int y0, int x1, int y1, rgb_t color);
void canvas_line(int x0, int y0, int x1, int y1, rgb_t color);
// Filled ellipse inside the inclusive bounding box (like ImageDraw.ellipse).
void canvas_ellipse(int x0, int y0, int x1, int y1, rgb_t color);
// Copy a w*h RGB888 image to (x, y).
void canvas_blit_rgb(int x, int y, int w, int h, const uint8_t *rgb);

// Drawing outside the clip rect (inclusive) is discarded. Reset with
// canvas_clip_reset().
void canvas_clip(int x0, int y0, int x1, int y1);
void canvas_clip_reset(void);

// Text is UTF-8; characters outside Latin-1 are approximated or shown as '?'.
// y is the top of the font cell. Returns the advance width.
int canvas_text_width(const font_t *font, const char *text);
int canvas_text(const font_t *font, int x, int y, const char *text,
                rgb_t color);
void canvas_text_centered(const font_t *font, int y, const char *text,
                          rgb_t color);
// Copy as much of `text` as fits in max_width pixels into out.
void canvas_text_fit(const font_t *font, const char *text, int max_width,
                     char *out, int out_size);

// Text in the band [x0, x1] that scrolls right-to-left if it does not fit,
// t_ms since the app started. Returns how long one full scroll takes (0 if
// the text fits and is drawn centered instead).
int canvas_marquee(const font_t *font, int x0, int x1, int y, const char *text,
                   rgb_t color, uint32_t t_ms);

// Push the framebuffer to the LED matrix.
void canvas_present(void);
