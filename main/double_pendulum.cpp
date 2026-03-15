#include "double_pendulum.hpp"

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#include "display.h"
#include "double_pendulum.h"

static const char* TAG = "double_pendulum";

#define DISPLAY_WIDTH 64
#define DISPLAY_HEIGHT 32

#define PIVOT_X 32
#define PIVOT_Y 6
#define ARM1_LENGTH 12
#define ARM2_LENGTH 10

#define FRAME_DELAY_MS 10

static dp::state current_state;
static dp::system pendulum_system;
static double sim_time = 0.0;
static uint8_t hue_counter = 0;

static void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t* r, uint8_t* g,
                       uint8_t* b) {
  uint8_t region = h / 43;
  uint8_t remainder = (h - (region * 43)) * 6;
  uint8_t p = (v * (255 - s)) / 255;
  uint8_t q = (v * (255 - ((s * remainder) / 256))) / 255;
  uint8_t t = (v * (255 - ((s * (256 - remainder)) / 256))) / 255;

  switch (region) {
    case 0:
      *r = v;
      *g = t;
      *b = p;
      break;
    case 1:
      *r = q;
      *g = v;
      *b = p;
      break;
    case 2:
      *r = p;
      *g = v;
      *b = t;
      break;
    case 3:
      *r = p;
      *g = q;
      *b = v;
      break;
    case 4:
      *r = t;
      *g = p;
      *b = v;
      break;
    default:
      *r = v;
      *g = p;
      *b = q;
      break;
  }
}

static void draw_line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g,
                      uint8_t b) {
  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  int sx = (x0 < x1) ? 1 : -1;
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx - dy;

  while (1) {
    if (x0 >= 0 && x0 < DISPLAY_WIDTH && y0 >= 0 && y0 < DISPLAY_HEIGHT) {
      display_get_matrix()->drawPixelRGB888(x0, y0, r, g, b);
    }

    if (x0 == x1 && y0 == y1) break;

    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static void draw_bob(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  // Draw a more circular bob with radius 2 pixels
  for (int dy = -2; dy <= 2; dy++) {
    for (int dx = -2; dx <= 2; dx++) {
      int px = x + dx;
      int py = y + dy;
      // Check if point is within circle radius 2
      if (dx * dx + dy * dy <= 4 &&  // radius squared = 4
          px >= 0 && px < DISPLAY_WIDTH && py >= 0 && py < DISPLAY_HEIGHT) {
        display_get_matrix()->drawPixelRGB888(px, py, r, g, b);
      }
    }
  }
}

void dp_init(void) {
  ESP_LOGI(TAG, "Initializing double pendulum");

  pendulum_system.mass.first = 1.0;
  pendulum_system.mass.second = 1.0;
  pendulum_system.length.first = 1.0;
  pendulum_system.length.second = 1.0;

  // Start with lower energy - angles closer to vertical (hanging down)
  // But add some randomness to avoid always starting the same way
  // Range: 10-80 degrees from vertical (so mostly down but with some swing)
  double angle1_offset = 70.0 + (esp_random() % 30);  // 10-80 degrees
  double angle2_offset = 100.0 + (esp_random() % 30);  // 10-80 degrees

  // Randomly choose direction (left or right) for each arm
  if (esp_random() % 2) angle1_offset = -angle1_offset;
  if (esp_random() % 2) angle2_offset = -angle2_offset;

  // Convert from vertical: 0 degrees = straight down, 90 degrees = horizontal
  current_state.theta.first = angle1_offset * 3.14159 / 180.0;
  current_state.theta.second = angle2_offset * 3.14159 / 180.0;
  current_state.omega.first = 0.0;
  current_state.omega.second = 0.0;

  sim_time = 0.0;
  hue_counter = esp_random() % 255;  // Start at random hue

  ESP_LOGI(TAG,
           "Initial angles: theta1=%.2f rad (%.1f° from vertical), theta2=%.2f "
           "rad (%.1f° from vertical)",
           current_state.theta.first, angle1_offset, current_state.theta.second,
           angle2_offset);
}

void dp_run(void) {
  ESP_LOGI(TAG, "Starting double pendulum animation");

  dp_init();

  while (1) {
    current_state = dp::advance(current_state, pendulum_system, 0.01);
    sim_time += 0.01;

    double x1 = PIVOT_X + ARM1_LENGTH * sin(current_state.theta.first);
    double y1 = PIVOT_Y + ARM1_LENGTH * cos(current_state.theta.first);

    double x2 = x1 + ARM2_LENGTH * sin(current_state.theta.second);
    double y2 = y1 + ARM2_LENGTH * cos(current_state.theta.second);

    int ix1 = (int)(x1 + 0.5);
    int iy1 = (int)(y1 + 0.5);
    int ix2 = (int)(x2 + 0.5);
    int iy2 = (int)(y2 + 0.5);

    display_clear();

    draw_line(PIVOT_X, PIVOT_Y, ix1, iy1, 255, 255, 255);
    draw_line(ix1, iy1, ix2, iy2, 255, 255, 255);

    draw_bob(ix1, iy1, 0, 255, 255);

    uint8_t r, g, b;
    hsv_to_rgb(hue_counter, 255, 255, &r, &g, &b);
    draw_bob(ix2, iy2, r, g, b);

    hue_counter++;
    if (hue_counter == 0) {
      hue_counter = 1;
    }

    display_flip();

    vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
  }
}
