#include "display.h"
#include "font5x7.h"
#include "nvs_settings.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <hub75.h>

static Hub75Driver *_matrix;
static uint8_t _brightness = (CONFIG_HUB75_BRIGHTNESS * 100) / 255;
static const char *TAG = "display";

#if CONFIG_HUB75_PANEL_WIDTH == 128 && CONFIG_HUB75_PANEL_HEIGHT == 64
static uint32_t *_scaled_buffer = NULL;
#endif

int display_initialize(void) {
#if CONFIG_HUB75_PANEL_WIDTH == 128 && CONFIG_HUB75_PANEL_HEIGHT == 64
  if (_scaled_buffer == NULL) {
    _scaled_buffer = (uint32_t *)heap_caps_malloc(128 * 64 * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (_scaled_buffer == NULL) {
      ESP_LOGE(TAG, "Failed to allocate scaled buffer in PSRAM");
      return 1;
    }
  }
#endif

  // Get swap_colors setting
  bool swap_colors = nvs_get_swap_colors();

  // Initialize pin values based on hardware and swap_colors setting
  ESP_LOGI(TAG, "Initializing display with swap_colors=%s", swap_colors ? "true" : "false");

  // Initialize the panel.
  Hub75Config mxconfig;
  mxconfig.panel_width = CONFIG_HUB75_PANEL_WIDTH;
  mxconfig.panel_height = CONFIG_HUB75_PANEL_HEIGHT;

#if CONFIG_BOARD_TIDBYT_GEN2
  mxconfig.pins.r1 = 5;
  mxconfig.pins.g1 = 23;
  mxconfig.pins.b1 = 4;
  mxconfig.pins.r2 = 2;
  mxconfig.pins.g2 = 22;
  mxconfig.pins.b2 = 32;
  mxconfig.pins.a = 25;
  mxconfig.pins.b = 21;
  mxconfig.pins.c = 26;
  mxconfig.pins.d = 19;
  mxconfig.pins.e = -1;  // assign to pin 14 if using more than two panels
  mxconfig.pins.lat = 18;
  mxconfig.pins.oe = 27;
  mxconfig.pins.clk = 15;
  ESP_LOGI(TAG, "Board preset: Tidbyt Gen2");
#elif CONFIG_BOARD_TRONBYT_S3_WIDE
  mxconfig.pins.r1 = 4;
  mxconfig.pins.g1 = 5;
  mxconfig.pins.b1 = 6;
  mxconfig.pins.r2 = 7;
  mxconfig.pins.g2 = 15;
  mxconfig.pins.b2 = 16;
  mxconfig.pins.a = 17;
  mxconfig.pins.b = 18;
  mxconfig.pins.c = 8;
  mxconfig.pins.d = 3;
  mxconfig.pins.e = 46;
  mxconfig.pins.lat = 9;
  mxconfig.pins.oe = 10;
  mxconfig.pins.clk = 11;
  ESP_LOGI(TAG, "Board preset: Tronbyt S3 Wide");
#elif CONFIG_BOARD_TRONBYT_S3
  mxconfig.pins.r1 = 4;
  mxconfig.pins.g1 = 6;
  mxconfig.pins.b1 = 5;
  mxconfig.pins.r2 = 7;
  mxconfig.pins.g2 = 16;
  mxconfig.pins.b2 = 15;
  mxconfig.pins.a = 17;
  mxconfig.pins.b = 18;
  mxconfig.pins.c = 8;
  mxconfig.pins.d = 3;
  mxconfig.pins.e = -1;
  mxconfig.pins.lat = 9;
  mxconfig.pins.oe = 10;
  mxconfig.pins.clk = 11;
  ESP_LOGI(TAG, "Board preset: Tronbyt S3");
#elif CONFIG_BOARD_PIXOTICKER
  mxconfig.pins.r1 = 2;
  mxconfig.pins.g1 = 4;
  mxconfig.pins.b1 = 15;
  mxconfig.pins.r2 = 16;
  mxconfig.pins.g2 = 17;
  mxconfig.pins.b2 = 27;
  mxconfig.pins.a = 5;
  mxconfig.pins.b = 18;
  mxconfig.pins.c = 19;
  mxconfig.pins.d = 21;
  mxconfig.pins.e = 12;
  mxconfig.pins.lat = 26;
  mxconfig.pins.oe = 25;
  mxconfig.pins.clk = 22;
  ESP_LOGI(TAG, "Board preset: Pixoticker");
#elif CONFIG_BOARD_MATRIXPORTAL_S3
  mxconfig.pins.r1 = 42;
  mxconfig.pins.r2 = 38;
  mxconfig.pins.a = 45;
  mxconfig.pins.b = 36;
  mxconfig.pins.c = 48;
  mxconfig.pins.d = 35;
  mxconfig.pins.e = 21;
  mxconfig.pins.lat = 47;
  mxconfig.pins.oe = 14;
  mxconfig.pins.clk = 2;
  if (swap_colors) {
    mxconfig.pins.g1 = 41;
    mxconfig.pins.b1 = 40;
    mxconfig.pins.g2 = 39;
    mxconfig.pins.b2 = 37;
  } else {
    mxconfig.pins.g1 = 40;
    mxconfig.pins.b1 = 41;
    mxconfig.pins.g2 = 37;
    mxconfig.pins.b2 = 39;
  }
  ESP_LOGI(TAG, "Board preset: MatrixPortal S3");
#else // GEN1 from here down.
  mxconfig.pins.a = 26;
  mxconfig.pins.b = 5;
  mxconfig.pins.c = 25;
  mxconfig.pins.d = 18;
  mxconfig.pins.e = -1;  // assign to pin 14 if using more than two panels
  mxconfig.pins.lat = 19;
  mxconfig.pins.oe = 32;
  mxconfig.pins.clk = 33;
  if (swap_colors) {
    // Swapped configuration for GEN1
    mxconfig.pins.r1 = 21;
    mxconfig.pins.g1 = 2;
    mxconfig.pins.b1 = 22;
    mxconfig.pins.r2 = 23;
    mxconfig.pins.g2 = 4;
    mxconfig.pins.b2 = 27;
  } else {
    // Normal configuration for GEN1
    mxconfig.pins.r1 = 2;
    mxconfig.pins.g1 = 22;
    mxconfig.pins.b1 = 21;
    mxconfig.pins.r2 = 4;
    mxconfig.pins.g2 = 27;
    mxconfig.pins.b2 = 23;
  }
  ESP_LOGI(TAG, "Board preset: Tidbyt Gen1");
#endif

  // Scan Pattern
#if defined(CONFIG_HUB75_SCAN_1_32)
  mxconfig.scan_pattern = Hub75ScanPattern::SCAN_1_32;
#elif defined(CONFIG_HUB75_SCAN_1_16)
  mxconfig.scan_pattern = Hub75ScanPattern::SCAN_1_16;
#elif defined(CONFIG_HUB75_SCAN_1_8)
  mxconfig.scan_pattern = Hub75ScanPattern::SCAN_1_8;
#endif

  // Scan wiring
#if defined(CONFIG_HUB75_WIRING_STANDARD)
  mxconfig.scan_wiring = Hub75ScanWiring::STANDARD_TWO_SCAN;
#elif defined(CONFIG_HUB75_WIRING_FOUR_SCAN_16PX)
  mxconfig.scan_wiring = Hub75ScanWiring::FOUR_SCAN_16PX_HIGH;
#elif defined(CONFIG_HUB75_WIRING_FOUR_SCAN_32PX)
  mxconfig.scan_wiring = Hub75ScanWiring::FOUR_SCAN_32PX_HIGH;
#elif defined(CONFIG_HUB75_WIRING_FOUR_SCAN_64PX)
  mxconfig.scan_wiring = Hub75ScanWiring::FOUR_SCAN_64PX_HIGH;
#endif

  // Shift Driver
#if defined(CONFIG_HUB75_DRIVER_GENERIC)
  mxconfig.shift_driver = Hub75ShiftDriver::GENERIC;
#elif defined(CONFIG_HUB75_DRIVER_FM6126A)
  mxconfig.shift_driver = Hub75ShiftDriver::FM6126A;
#elif defined(CONFIG_HUB75_DRIVER_FM6124)
  mxconfig.shift_driver = Hub75ShiftDriver::FM6124;
#elif defined(CONFIG_HUB75_DRIVER_MBI5124)
  mxconfig.shift_driver = Hub75ShiftDriver::MBI5124;
#elif defined(CONFIG_HUB75_DRIVER_DP3246)
  mxconfig.shift_driver = Hub75ShiftDriver::DP3246;
#endif

#if CONFIG_HUB75_DOUBLE_BUFFER
  mxconfig.double_buffer = true;
#else
  mxconfig.double_buffer = false;
#endif

  // Clock Speed
#if defined(CONFIG_HUB75_CLK_32MHZ)
  mxconfig.output_clock_speed = Hub75ClockSpeed::HZ_32M;
#elif defined(CONFIG_HUB75_CLK_20MHZ)
  mxconfig.output_clock_speed = Hub75ClockSpeed::HZ_20M;
#elif defined(CONFIG_HUB75_CLK_16MHZ)
  mxconfig.output_clock_speed = Hub75ClockSpeed::HZ_16M;
#elif defined(CONFIG_HUB75_CLK_10MHZ)
  mxconfig.output_clock_speed = Hub75ClockSpeed::HZ_10M;
#elif defined(CONFIG_HUB75_CLK_8MHZ)
  mxconfig.output_clock_speed = Hub75ClockSpeed::HZ_8M;
#endif

  mxconfig.min_refresh_rate = CONFIG_HUB75_MIN_REFRESH_RATE;
  mxconfig.latch_blanking = CONFIG_HUB75_LATCH_BLANKING;

  // Clock Phase
#ifdef CONFIG_HUB75_CLK_PHASE_INVERTED
  mxconfig.clk_phase_inverted = true;
#else
  mxconfig.clk_phase_inverted = false;
#endif

  mxconfig.brightness = CONFIG_HUB75_BRIGHTNESS;

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
  display_set_brightness((CONFIG_HUB75_BRIGHTNESS * 100) / 255);

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

void display_fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
  if (_matrix != NULL) {
    _matrix->fill(x, y, w, h, r, g, b);
    // Note: No flip here, caller must flip
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
