#include "display.h"
#include "font5x7.h"
#include "nvs_settings.h"
#include "esp_log.h"

#include <hub75.h>
#if CONFIG_BOARD_TIDBYT_GEN2
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
#elif CONFIG_BOARD_TRONBYT_S3_WIDE
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

  #define WIDTH 128
  #define HEIGHT 64
#elif CONFIG_BOARD_TRONBYT_S3
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
#elif CONFIG_BOARD_PIXOTICKER
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
#elif CONFIG_BOARD_MATRIXPORTAL_S3
//                     R1, G1, B1, R2, G2, B2
// uint8_t rgbPins[] = {42, 41, 40, 38, 39, 37};
// uint8_t addrPins[] = {45, 36, 48, 35, 21};
// uint8_t clockPin = 2;
// uint8_t latchPin = 47;
// uint8_t oePin = 14;
  #define R1 42
  #define R2 38
  #define CH_A 45
  #define CH_B 36
  #define CH_C 48
  #define CH_D 35
  #define CH_E 21
  #define CLK 2
  #define LAT 47
  #define OE 14
#else // GEN1 from here down.
  #define CH_A 26
  #define CH_B 5
  #define CH_C 25
  #define CH_D 18
  #define CH_E -1  // assign to pin 14 if using more than two panels

  #define LAT 19
  #define OE 32
  #define CLK 33
#endif

static Hub75Driver *_matrix;
static uint8_t _brightness = DEFAULT_BRIGHTNESS;
static const char *TAG = "display";

#if CONFIG_HUB75_PANEL_WIDTH == 128 && CONFIG_HUB75_PANEL_HEIGHT == 64
static uint32_t _scaled_buffer[128 * 64];
#endif

int display_initialize(void) {
  // Get swap_colors setting
  bool swap_colors = nvs_get_swap_colors();

  // Initialize pin values based on hardware and swap_colors setting
  int8_t pin_R1, pin_G1, pin_BL1, pin_R2, pin_G2, pin_BL2;

#if CONFIG_BOARD_MATRIXPORTAL_S3
  pin_R1 = R1;  // R1 = 42
  pin_R2 = R2;  // R2 = 38
  if (swap_colors) {
    // Swapped configuration for MATRIXPORTALS3
    pin_G1 = 40;
    pin_BL1 = 41;
    pin_G2 = 37;
    pin_BL2 = 39;
  } else {
    // Normal configuration for MATRIXPORTALS3
    pin_G1 = 41;
    pin_BL1 = 40;
    pin_G2 = 39;
    pin_BL2 = 37;
  }
#elif CONFIG_BOARD_TIDBYT_GEN2 || CONFIG_BOARD_TRONBYT_S3_WIDE || CONFIG_BOARD_TRONBYT_S3 || CONFIG_BOARD_PIXOTICKER
  // These variants don't support color swapping, use fixed pins
  pin_R1 = R1;
  pin_G1 = G1;
  pin_BL1 = BL1;
  pin_R2 = R2;
  pin_G2 = G2;
  pin_BL2 = BL2;
#else // GEN1
  if (swap_colors) {
    // Swapped configuration for GEN1
    pin_R1 = 21;
    pin_G1 = 2;
    pin_BL1 = 22;
    pin_R2 = 23;
    pin_G2 = 4;
    pin_BL2 = 27;
  } else {
    // Normal configuration for GEN1
    pin_R1 = 2;
    pin_G1 = 22;
    pin_BL1 = 21;
    pin_R2 = 4;
    pin_G2 = 27;
    pin_BL2 = 23;
  }
#endif

  ESP_LOGI(TAG, "Initializing display with swap_colors=%s", swap_colors ? "true" : "false");

  // Initialize the panel.
  Hub75Pins pins = {
    .r1 = pin_R1, .g1 = pin_G1, .b1 = pin_BL1,
    .r2 = pin_R2, .g2 = pin_G2, .b2 = pin_BL2,
    .a = CH_A, .b = CH_B, .c = CH_C, .d = CH_D, .e = CH_E,
    .lat = LAT, .oe = OE, .clk = CLK
  };

  Hub75Config mxconfig;
  mxconfig.panel_width = CONFIG_HUB75_PANEL_WIDTH;
  mxconfig.panel_height = CONFIG_HUB75_PANEL_HEIGHT;
  mxconfig.pins = pins;

  // Scan Pattern
#if defined(CONFIG_HUB75_SCAN_1_32)
  mxconfig.scan_pattern = Hub75ScanPattern::SCAN_1_32;
#elif defined(CONFIG_HUB75_SCAN_1_16)
  mxconfig.scan_pattern = Hub75ScanPattern::SCAN_1_16;
#elif defined(CONFIG_HUB75_SCAN_1_8)
  mxconfig.scan_pattern = Hub75ScanPattern::SCAN_1_8;
#else
  mxconfig.scan_pattern = (CONFIG_HUB75_PANEL_HEIGHT == 64) ? Hub75ScanPattern::SCAN_1_32 : Hub75ScanPattern::SCAN_1_16;
#endif

  // Shift Driver
#if defined(CONFIG_HUB75_DRIVER_FM6126A)
  mxconfig.shift_driver = Hub75ShiftDriver::FM6126A;
#elif defined(CONFIG_HUB75_DRIVER_FM6124)
  mxconfig.shift_driver = Hub75ShiftDriver::FM6124;
#elif defined(CONFIG_HUB75_DRIVER_MBI5124)
  mxconfig.shift_driver = Hub75ShiftDriver::MBI5124;
#elif defined(CONFIG_HUB75_DRIVER_DP3246)
  mxconfig.shift_driver = Hub75ShiftDriver::DP3246;
#else
  mxconfig.shift_driver = Hub75ShiftDriver::NORMAL;
#endif

  mxconfig.double_buffer = true;

  // Clock Speed
#if defined(CONFIG_HUB75_CLK_20MHZ)
  mxconfig.output_clock_speed = Hub75ClockSpeed::HZ_20M;
#elif defined(CONFIG_HUB75_CLK_16MHZ)
  mxconfig.output_clock_speed = Hub75ClockSpeed::HZ_16M;
#elif defined(CONFIG_HUB75_CLK_10MHZ)
  mxconfig.output_clock_speed = Hub75ClockSpeed::HZ_10M;
#elif defined(CONFIG_HUB75_CLK_8MHZ)
  mxconfig.output_clock_speed = Hub75ClockSpeed::HZ_8M;
#else
  mxconfig.output_clock_speed = Hub75ClockSpeed::HZ_8M;
#endif

  mxconfig.latch_blanking = CONFIG_HUB75_LATCH_BLANKING;

  // Clock Phase
#ifdef CONFIG_HUB75_CLK_PHASE_INVERTED
  mxconfig.clk_phase_inverted = true;
#else
  mxconfig.clk_phase_inverted = false;
#endif

  mxconfig.brightness = DEFAULT_BRIGHTNESS;

  _matrix = new Hub75Driver(mxconfig);

  if (_matrix == NULL) {
    ESP_LOGE(TAG, "Failed to allocate Hub75Driver object");
    return 1;
  }

  if (!_matrix->begin()) {
    ESP_LOGE(TAG, "Hub75Driver begin() failed");
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
    _matrix->set_brightness(brightness_8bit);
    _matrix->clear();
    _brightness = brightness_pct;
  }
}

void display_shutdown(void) {
  _matrix->clear();
  _matrix->end();
  delete _matrix;
  _matrix = NULL;
}

void display_draw(const uint8_t *pix, int width, int height) {
#if CONFIG_HUB75_PANEL_WIDTH == 128 && CONFIG_HUB75_PANEL_HEIGHT == 64
  if (width == 64 && height == 32) {
    // Optimize scale-by-2 drawing (specifically for 64x32 -> 128x64)
    const uint32_t *src32 = (const uint32_t *)pix;
    for (int y = 0; y < height; y++) {
      uint32_t *dst_row1 = &_scaled_buffer[(y * 2) * 128];
      uint32_t *dst_row2 = &_scaled_buffer[(y * 2 + 1) * 128];
      for (int x = 0; x < width; x++) {
        uint32_t pixel = src32[y * width + x];
        // Fill 2x2 block
        dst_row1[x * 2] = pixel;
        dst_row1[x * 2 + 1] = pixel;
        dst_row2[x * 2] = pixel;
        dst_row2[x * 2 + 1] = pixel;
      }
    }

    _matrix->draw_pixels(0, 0, 128, 64, (uint8_t*)_scaled_buffer, Hub75PixelFormat::RGB888_32, Hub75ColorOrder::BGR);
    _matrix->flip_buffer();
    return;
  }
#endif

  // Default path: bulk transfer for native resolution
  _matrix->draw_pixels(0, 0, width, height, pix, Hub75PixelFormat::RGB888_32, Hub75ColorOrder::BGR);
  _matrix->flip_buffer();
}

void display_clear(void) { _matrix->clear(); }

void display_draw_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if (_matrix != NULL) {
    _matrix->set_pixel(x, y, r, g, b);
    _matrix->flip_buffer();
  }
}

void draw_error_indicator_pixel(void) {
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
          int px = cursor_x + (col * scale);
          int py = cursor_y + (row * scale);

          if (scale > 1) {
             // Optimize scaled text using fill
             _matrix->fill(px, py, scale, scale, r, g, b);
          } else {
             // Draw pixel(s) based on scale
             // Check bounds
             if (px >= 0 && px < CONFIG_HUB75_PANEL_WIDTH && py >= 0 && py < CONFIG_HUB75_PANEL_HEIGHT) {
               _matrix->set_pixel(px, py, r, g, b);
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

void display_flip(void) {
  if (_matrix != NULL) {
    _matrix->flip_buffer();
  }
}
