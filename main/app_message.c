#include "app_api.h"

uint32_t message_draw(const char *title, const char *text, rgb_t color,
                      uint32_t t_ms) {
  if (title && title[0]) {
    canvas_rect(0, 0, CANVAS_W - 1, 10, RGB(40, 40, 60));
    char fitted[48];
    canvas_text_fit(&font_tb8, title, CANVAS_W, fitted, sizeof(fitted));
    canvas_text_centered(&font_tb8, 2, fitted, RGB(255, 200, 0));
    return canvas_marquee(&font_6x13, 0, CANVAS_W - 1, 16, text, color, t_ms);
  }
  return canvas_marquee(&font_6x13, 0, CANVAS_W - 1, 10, text, color, t_ms);
}

static bool message_prepare(const cJSON *cfg) {
  return cfg_str(cfg, "text", "")[0] != '\0';
}

static uint32_t message_app_draw(const cJSON *cfg, uint32_t t_ms) {
  return message_draw(cfg_str(cfg, "title", ""), cfg_str(cfg, "text", ""),
                      parse_color(cfg_str(cfg, "color", NULL),
                                  RGB(255, 255, 255)),
                      t_ms);
}

const app_t app_message = {"message", message_prepare, message_app_draw};
