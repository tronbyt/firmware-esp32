#include "canvas.h"

#include <stdlib.h>
#include <string.h>

#include "display.h"

#define MARQUEE_MS_PER_PX 40

static uint8_t s_fb[CANVAS_H][CANVAS_W][3];
static int s_cx0 = 0, s_cy0 = 0, s_cx1 = CANVAS_W - 1, s_cy1 = CANVAS_H - 1;

void canvas_clip(int x0, int y0, int x1, int y1) {
  s_cx0 = x0 < 0 ? 0 : x0;
  s_cy0 = y0 < 0 ? 0 : y0;
  s_cx1 = x1 >= CANVAS_W ? CANVAS_W - 1 : x1;
  s_cy1 = y1 >= CANVAS_H ? CANVAS_H - 1 : y1;
}

void canvas_clip_reset(void) { canvas_clip(0, 0, CANVAS_W - 1, CANVAS_H - 1); }

void canvas_clear(rgb_t c) {
  for (int y = 0; y < CANVAS_H; y++) {
    for (int x = 0; x < CANVAS_W; x++) {
      s_fb[y][x][0] = c.r;
      s_fb[y][x][1] = c.g;
      s_fb[y][x][2] = c.b;
    }
  }
}

void canvas_pixel(int x, int y, rgb_t c) {
  if (x < s_cx0 || x > s_cx1 || y < s_cy0 || y > s_cy1) return;
  s_fb[y][x][0] = c.r;
  s_fb[y][x][1] = c.g;
  s_fb[y][x][2] = c.b;
}

void canvas_rect(int x0, int y0, int x1, int y1, rgb_t c) {
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) canvas_pixel(x, y, c);
  }
}

void canvas_line(int x0, int y0, int x1, int y1, rgb_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    canvas_pixel(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void canvas_ellipse(int x0, int y0, int x1, int y1, rgb_t c) {
  // Test pixel centres against the ellipse inscribed in the box's outer edges.
  float cx = (x0 + x1 + 1) / 2.0f, cy = (y0 + y1 + 1) / 2.0f;
  float rx = (x1 - x0 + 1) / 2.0f, ry = (y1 - y0 + 1) / 2.0f;
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      float nx = (x + 0.5f - cx) / rx, ny = (y + 0.5f - cy) / ry;
      if (nx * nx + ny * ny <= 1.0f) canvas_pixel(x, y, c);
    }
  }
}

void canvas_blit_rgb(int x, int y, int w, int h, const uint8_t *rgb) {
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      const uint8_t *p = rgb + (j * w + i) * 3;
      canvas_pixel(x + i, y + j, RGB(p[0], p[1], p[2]));
    }
  }
}

// Decode one UTF-8 character to a Latin-1 code (approximating common
// typography outside Latin-1). Advances *s.
static uint8_t next_char(const char **s) {
  const uint8_t *p = (const uint8_t *)*s;
  uint32_t cp;
  int n;
  if (p[0] < 0x80) {
    cp = p[0];
    n = 1;
  } else if ((p[0] & 0xE0) == 0xC0 && p[1]) {
    cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    n = 2;
  } else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
    cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    n = 3;
  } else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
    cp = '?';
    n = 4;
  } else {
    cp = '?';
    n = 1;
  }
  *s += n;
  if (cp <= 0xFF) return (uint8_t)cp;
  switch (cp) {
    case 0x2013:  // en dash
    case 0x2014:  // em dash
      return '-';
    case 0x2018:
    case 0x2019:
      return '\'';
    case 0x201C:
    case 0x201D:
      return '"';
    case 0x2026:  // ellipsis
      return '.';
    default:
      return '?';
  }
}

static const font_glyph_t *glyph(const font_t *font, uint8_t code) {
  if (code < font->first || code > font->last) code = '?';
  return &font->glyphs[code - font->first];
}

int canvas_text_width(const font_t *font, const char *text) {
  int w = 0;
  while (*text) w += glyph(font, next_char(&text))->advance;
  return w;
}

int canvas_text(const font_t *font, int x, int y, const char *text,
                rgb_t color) {
  int start = x;
  while (*text) {
    const font_glyph_t *g = glyph(font, next_char(&text));
    if (x + g->advance > s_cx0 && x <= s_cx1) {
      for (int row = 0; row < font->height; row++) {
        uint16_t bits = font->rows[g->offset + row];
        for (int col = 0; bits && col < g->advance; col++) {
          if (bits & (0x8000 >> col)) canvas_pixel(x + col, y + row, color);
        }
      }
    }
    x += g->advance;
  }
  return x - start;
}

void canvas_text_centered(const font_t *font, int y, const char *text,
                          rgb_t color) {
  canvas_text(font, (CANVAS_W - canvas_text_width(font, text)) / 2, y, text,
              color);
}

void canvas_text_fit(const font_t *font, const char *text, int max_width,
                     char *out, int out_size) {
  int w = 0, len = 0;
  const char *p = text;
  while (*p) {
    int adv = glyph(font, next_char(&p))->advance;
    if (w + adv > max_width || (p - text) > out_size - 1) break;
    w += adv;
    len = p - text;
  }
  memcpy(out, text, len);
  out[len] = '\0';
}

int canvas_marquee(const font_t *font, int x0, int x1, int y, const char *text,
                   rgb_t color, uint32_t t_ms) {
  int band = x1 - x0 + 1;
  int w = canvas_text_width(font, text);
  canvas_clip(x0, 0, x1, CANVAS_H - 1);
  if (w <= band) {
    canvas_text(font, x0 + (band - w) / 2, y, text, color);
    canvas_clip_reset();
    return 0;
  }
  // Enter from the right edge, leave on the left, then start over.
  int span = band + w + 1;
  int offset = (t_ms / MARQUEE_MS_PER_PX) % span;
  canvas_text(font, x1 + 1 - offset, y, text, color);
  canvas_clip_reset();
  return span * MARQUEE_MS_PER_PX;
}

void canvas_present(void) {
  display_draw(&s_fb[0][0][0], CANVAS_W, CANVAS_H, 3, 0, 1, 2);
}
