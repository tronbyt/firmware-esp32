// Host-side rendering test: draws reference scenes with the device's canvas
// and font code and writes them as PPM files, so they can be compared pixel by
// pixel with the Python tidbyt-server (see compare.py).
//
//   python -m ziglang cc -I main -I <idf>/components/json/cJSON \
//     test/host/render_test.c main/canvas.c main/fonts.c main/app_message.c \
//     <idf>/components/json/cJSON/cJSON.c -o render_test.exe

#include <stdio.h>
#include <string.h>

#include "app_api.h"
#include "canvas.h"

static const uint8_t *s_last;

// Stubs for what canvas.c / app_message.c use from the firmware.
void display_draw(const uint8_t *pix, int w, int h, int ch, int r, int g, int b) {
  s_last = pix;
}
const char *cfg_str(const cJSON *cfg, const char *key, const char *def) { return def; }
rgb_t parse_color(const char *hex, rgb_t def) { return def; }

static void save(const char *name) {
  canvas_present();
  char path[128];
  snprintf(path, sizeof(path), "test/host/out/%s.ppm", name);
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", CANVAS_W, CANVAS_H);
  fwrite(s_last, 1, CANVAS_W * CANVAS_H * 3, f);
  fclose(f);
}

int main(void) {
  rgb_t white = RGB(255, 255, 255);

  canvas_clear(RGB(0, 0, 0));
  canvas_text_centered(&font_10x20, 4, "19:43", white);
  canvas_text_centered(&font_tb8, 23, "Sun 11 Oct", RGB(255, 140, 60));
  save("clock");

  canvas_clear(RGB(0, 0, 0));
  canvas_text(&font_6x13, 25, 2, "12\xC2\xB0", white);
  canvas_text(&font_tb8, 25, 15, "Partly", RGB(130, 200, 255));
  canvas_text(&font_4x5, 25, 25, "191", RGB(255, 120, 80));
  canvas_text(&font_tb8, 0, 15, "\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F", white);  // äöüß
  save("text");

  canvas_clear(RGB(0, 0, 0));
  message_draw("Hello", "Running on my own server!", RGB(127, 219, 255), 60 * 40);
  save("marquee");

  canvas_clear(RGB(0, 0, 0));
  canvas_ellipse(6, 4, 17, 15, RGB(255, 200, 0));
  canvas_line(4, 2, 5, 3, RGB(255, 200, 0));
  canvas_rect(6, 15, 19, 19, RGB(150, 150, 160));
  save("shapes");
  printf("ok\n");
  return 0;
}
