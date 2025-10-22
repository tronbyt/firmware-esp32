#include "display.h"
#include "font5x7.h"

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#ifdef TIDBYT_GEN2
  #define R1 5
  #define G1 23
  #define BL1  4
  #define R2 2
  #define G2 22
  #define BL2 32

  #define CH_A 25
  #define CH_B 21
  #define CH_C 26
  #define CH_D 19
  #define CH_E -1  // assign to pin 14 if using more than two panels

  #define LAT 18
  #define OE 27
  #define CLK 15
#elif defined(TRONBYT_S3_WIDE)
  #define R1 4
  #define G1 5
  #define BL1 6
  #define R2 7
  #define G2 15
  #define BL2 16

  #define CH_A 17
  #define CH_B 18
  #define CH_C 8
  #define CH_D 3
  #define CH_E 46
  #define LAT 9
  #define OE 10
  #define CLK 11
#elif defined(TRONBYT_S3)
  #define R1 4
  #define G1 6
  #define BL1 5
  #define R2 7
  #define G2 16
  #define BL2 15

  #define CH_A 17
  #define CH_B 18
  #define CH_C 8
  #define CH_D 3
  #define CH_E -1

  #define LAT 9
  #define OE 10
  #define CLK 11
#elif defined(PIXOTICKER)
  #define R1 2
  #define G1 4
  #define BL1 15
  #define R2 16
  #define G2 17
  #define BL2 27
  #define CH_A 5
  #define CH_B 18
  #define CH_C 19
  #define CH_D 21
  #define CH_E 12
  #define CLK 22
  #define LAT 26
  #define OE 25
  #elif defined(MATRIXPORTALS3)
//                     R1, G1, B1, R2, G2, B2
// uint8_t rgbPins[] = {42, 41, 40, 38, 39, 37};
// uint8_t addrPins[] = {45, 36, 48, 35, 21};
// uint8_t clockPin = 2;
// uint8_t latchPin = 47;
// uint8_t oePin = 14;
  #define R1 42
  #define G1 41
  #define BL1 40
  #define R2 38
  #define G2 39
  #define BL2 37
  #define CH_A 45
  #define CH_B 36
  #define CH_C 48
  #define CH_D 35
  #define CH_E 21
  #define CLK 2
  #define LAT 47
  #define OE 14
#ifdef SWAP_COLORS
    #define G1 40
    #define BL1 41
    #define G2 37
    #define BL2 39
  #else
    #define G1 41
    #define BL1 40
    #define G2 39
    #define BL2 37
  #endif
#else // GEN1 from here down.
  #ifdef SWAP_COLORS
    #define R1 21
    #define G1 2
    #define BL1 22
    #define R2 23
    #define G2 4
    #define BL2 27
  #else
    #define R1 2
    #define G1 22
    #define BL1 21
    #define R2 4
    #define G2 27
    #define BL2 23
  #endif

#define CH_A 26
#define CH_B 5
#define CH_C 25
#define CH_D 18
#define CH_E -1  // assign to pin 14 if using more than two panels

#define LAT 19
#define OE 32
#define CLK 33
#endif

static MatrixPanel_I2S_DMA *_matrix;
static uint8_t _brightness = DISPLAY_DEFAULT_BRIGHTNESS;
static const char *TAG = "display";

int display_initialize() {
  // Initialize the panel.
  HUB75_I2S_CFG::i2s_pins pins = {R1,   G1,   BL1,  R2,   G2,  BL2, CH_A,
                                  CH_B, CH_C, CH_D, CH_E, LAT, OE,  CLK};

  #ifdef NO_INVERT_CLOCK_PHASE
  bool invert_clock_phase = false;
  #else
  bool invert_clock_phase = true;
  #endif

  #ifdef TRONBYT_S3_WIDE
  HUB75_I2S_CFG mxconfig(128,                     // width
                         64,                      // height
                         1,                       // chain length
                         pins,                    // pin mapping
                         HUB75_I2S_CFG::FM6126A,  // driver chip
                         true,                    // double-buffering
                         HUB75_I2S_CFG::HZ_10M,   // clock speed
                         1,                       // latch blanking
                         invert_clock_phase       // invert clock phase
  );
  #else
  HUB75_I2S_CFG mxconfig(64,                      // width
                         32,                      // height
                         1,                       // chain length
                         pins,                    // pin mapping
                         HUB75_I2S_CFG::FM6126A,  // driver chip
                         true,                    // double-buffering
                         HUB75_I2S_CFG::HZ_10M,   // clock speed
                         1,                       // latch blanking
                         invert_clock_phase       // invert clock phase
  );
  #endif

  _matrix = new MatrixPanel_I2S_DMA(mxconfig);

  if (_matrix == NULL) { // Should not happen with new if it throws std::bad_alloc
    ESP_LOGE(TAG, "Failed to allocate MatrixPanel_I2S_DMA object");
    return 1;
  }

  if (!_matrix->begin()) {
    ESP_LOGE(TAG, "MatrixPanel_I2S_DMA begin() failed");
    delete _matrix;
    _matrix = NULL;
    return 1;
  }
  display_set_brightness(DEFAULT_BRIGHTNESS);

  return 0;
}

static inline uint8_t brightness_percent_to_8bit(uint8_t pct) {
  if (pct > 100) pct = 100;
  return (uint8_t)(((uint32_t)pct * 230 + 50) / 100); // 230 as MAX 8 BIT HARDCODED
}

void display_set_brightness(uint8_t brightness_pct) {
  if (brightness_pct != _brightness) {
    uint8_t brightness_8bit = brightness_percent_to_8bit(brightness_pct);

#ifdef MAX_BRIGHTNESS_8BIT
    uint8_t max_brightness_8bit = MAX_BRIGHTNESS_8BIT;
    if (brightness_8bit > max_brightness_8bit) {
      brightness_8bit = max_brightness_8bit;
      ESP_LOGI(TAG, "Clamping brightness to MAX_BRIGHTNESS (%d)", MAX_BRIGHTNESS_8BIT);
    }
#endif

    ESP_LOGI(TAG, "Setting brightness to %d%% (%d)", brightness_pct, brightness_8bit);
    _matrix->setBrightness8(brightness_8bit);
    _matrix->clearScreen();
    _brightness = brightness_pct;
  }
}

uint8_t display_get_brightness() { return _brightness; }

void display_shutdown() {
  _matrix->clearScreen();
  _matrix->stopDMAoutput();
  delete _matrix;
  _matrix = NULL;
}

void display_draw(const uint8_t *pix, int width, int height,
                 int channels, int ixR, int ixG, int ixB) {
  int scale = 1;
  #ifdef TRONBYT_S3_WIDE
  if (width == 64 && height == 32) {
    scale = 2; // Scale up to 128x64
  }
  #endif

  for (unsigned int i = 0; i < height; i++) {
    for (unsigned int j = 0; j < width; j++) {
      const uint8_t *p = &pix[(i * width + j) * channels];
      uint8_t r = p[ixR];
      uint8_t g = p[ixG];
      uint8_t b = p[ixB];

      // Draw each pixel scaled up (2x2 pixels for each original pixel)
      for (int sy = 0; sy < scale; sy++) {
        for (int sx = 0; sx < scale; sx++) {
          _matrix->drawPixelRGB888(j * scale + sx, i * scale + sy, r, g, b);
        }
      }
    }
  }
  _matrix->flipDMABuffer();
}

void display_clear() { _matrix->clearScreen(); }

void display_draw_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if (_matrix != NULL) {
    _matrix->drawPixelRGB888(x, y, r, g, b);
    _matrix->flipDMABuffer();
  }
}

void draw_error_indicator_pixel() {
  display_draw_pixel(0, 0, 100, 0, 0);
}

void display_text(const char* text, int x, int y, uint8_t r, uint8_t g, uint8_t b, int scale) {
  if (_matrix == NULL || text == NULL) {
    return;
  }

  int cursor_x = x;
  int cursor_y = y;

  // Iterate through each character in the string
  for (int i = 0; text[i] != '\0'; i++) {
    char c = text[i];

    // Check if character is in font range
    if (c < FONT5X7_FIRST_CHAR || c > FONT5X7_LAST_CHAR) {
      c = ' '; // Replace unsupported characters with space
    }

    // Get font data for this character
    int char_index = c - FONT5X7_FIRST_CHAR;
    const uint8_t* char_data = font5x7[char_index];

    // Draw each column of the character
    for (int col = 0; col < FONT5X7_CHAR_WIDTH; col++) {
      uint8_t column_data = char_data[col];

      // Draw each row in the column
      for (int row = 0; row < FONT5X7_CHAR_HEIGHT; row++) {
        if (column_data & (1 << row)) {
          // Draw pixel(s) based on scale
          for (int sy = 0; sy < scale; sy++) {
            for (int sx = 0; sx < scale; sx++) {
              int px = cursor_x + (col * scale) + sx;
              int py = cursor_y + (row * scale) + sy;

              // Check bounds
              #ifdef TRONBYT_S3_WIDE
              if (px >= 0 && px < 128 && py >= 0 && py < 64) {
              #else
              if (px >= 0 && px < 64 && py >= 0 && py < 32) {
              #endif
                _matrix->drawPixelRGB888(px, py, r, g, b);
              }
            }
          }
        }
      }
    }

    // Move cursor to next character position (5 pixels + 1 pixel spacing)
    cursor_x += (FONT5X7_CHAR_WIDTH + 1) * scale;
  }

  // Note: Not flipping buffer here anymore - caller must call display_flip()
}

void display_flip() {
  if (_matrix != NULL) {
    _matrix->flipDMABuffer();
  }
}
