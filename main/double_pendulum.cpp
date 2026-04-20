#include "double_pendulum.hpp"

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#include "display.h"
#include "double_pendulum.h"
#include "nvs_settings.h"

static const char* TAG = "double_pendulum";

#define DISPLAY_WIDTH 64
#define DISPLAY_HEIGHT 32

#define PIVOT_X 32
#define PIVOT_Y 6

#define FRAME_DELAY_MS 10

static dp::state current_state;
static dp::system pendulum_system;
static double sim_time = 0.0;
static uint8_t hue_counter = 0;

#define TRAIL_LENGTH_MAX 2000

static int trail_x[TRAIL_LENGTH_MAX];
static int trail_y[TRAIL_LENGTH_MAX];
static uint8_t trail_hue[TRAIL_LENGTH_MAX];
static int trail_head = 0;
static int current_trail_length = 200;
static float current_arm1_length = 12.0f;
static float current_arm2_length = 10.0f;
static float arm1_length_target = 12.0f;
static float arm2_length_target = 10.0f;
static float arm1_length_min = 8.0f;
static float arm1_length_max = 14.0f;
static float arm2_length_min = 6.0f;
static float arm2_length_max = 12.0f;
static float arm_evolution_speed = 0.00002f;
static bool arm_evolution_setting = true;
static int arm_evolution_direction = 1;
static bool current_trail_color_cycle = true;
static float current_pendulum_speed = 0.01f;
static int current_brightness = 128;
static int current_leg_color = 0xFFFFFF;

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

  if (nvs_get_randomize_on_boot()) {
    nvs_set_trail_length(10 + esp_random() % 9990);
    nvs_set_pendulum_speed((esp_random() % 50 + 1) / 1000.0f);
    nvs_set_pendulum_arm1_length(esp_random() % 20 + 5);
    nvs_set_pendulum_arm2_length(esp_random() % 20 + 5);
    nvs_set_trail_color_cycle(esp_random() % 2);
    nvs_set_arm_evolution_enabled(true);
    nvs_set_arm_evolution_speed((esp_random() % 100 + 1) / 100000.0f);
    uint8_t r = esp_random() & 0xFF;
    uint8_t g = esp_random() & 0xFF;
    uint8_t b = esp_random() & 0xFF;
    nvs_set_leg_color((r << 16) | (g << 8) | b);
    nvs_save_settings();
  }

  // Get settings from NVS
  float arm1_len = nvs_get_pendulum_arm1_length();
  float arm2_len = nvs_get_pendulum_arm2_length();
  current_trail_length = nvs_get_trail_length();
  current_pendulum_speed = nvs_get_pendulum_speed();
  current_trail_color_cycle = nvs_get_trail_color_cycle();
  arm_evolution_setting = nvs_get_arm_evolution_enabled();
  arm_evolution_speed = nvs_get_arm_evolution_speed();
  current_brightness = nvs_get_brightness();
  current_leg_color = nvs_get_leg_color();
  if (current_trail_length > TRAIL_LENGTH_MAX) {
    current_trail_length = TRAIL_LENGTH_MAX;
  }

  pendulum_system.mass.first = nvs_get_pendulum_mass1();
  pendulum_system.mass.second = nvs_get_pendulum_mass2();
  current_arm1_length = arm1_len;
  current_arm2_length = arm2_len;
  pendulum_system.length.first = arm1_len / 10.0f;  // Scale to simulation units
  pendulum_system.length.second = arm2_len / 10.0f;

  for (int i = 0; i < TRAIL_LENGTH_MAX; i++) {
    trail_x[i] = -1;
    trail_y[i] = -1;
  }
  trail_head = 0;

  // Start with lower energy - angles closer to vertical (hanging down)
  // But add some randomness to avoid always starting the same way
  // Range: 10-80 degrees from vertical (so mostly down but with some swing)
  double angle1_offset = 70.0 + (esp_random() % 30);   // 10-80 degrees
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
    // Check if settings were updated via config portal
    if (nvs_settings_get_and_clear_reload_flag()) {
      ESP_LOGI(TAG, "Reloading settings from NVS");
      current_trail_length = nvs_get_trail_length();
      current_pendulum_speed = nvs_get_pendulum_speed();
      current_trail_color_cycle = nvs_get_trail_color_cycle();
      arm_evolution_setting = nvs_get_arm_evolution_enabled();
      arm_evolution_speed = nvs_get_arm_evolution_speed();
      current_brightness = nvs_get_brightness();
      current_leg_color = nvs_get_leg_color();
      current_arm1_length = nvs_get_pendulum_arm1_length();
      current_arm2_length = nvs_get_pendulum_arm2_length();
      pendulum_system.length.first = current_arm1_length / 10.0f;
      pendulum_system.length.second = current_arm2_length / 10.0f;
      pendulum_system.mass.first = nvs_get_pendulum_mass1();
      pendulum_system.mass.second = nvs_get_pendulum_mass2();
      if (current_trail_length > TRAIL_LENGTH_MAX) {
        current_trail_length = TRAIL_LENGTH_MAX;
      }
      // Reset trail and pendulum state
      for (int i = 0; i < TRAIL_LENGTH_MAX; i++) {
        trail_x[i] = -1;
        trail_y[i] = -1;
      }
      trail_head = 0;
      // Reinitialize pendulum with new parameters
      double angle1_offset = 70.0 + (esp_random() % 30);
      double angle2_offset = 100.0 + (esp_random() % 30);
      if (esp_random() % 2) angle1_offset = -angle1_offset;
      if (esp_random() % 2) angle2_offset = -angle2_offset;
      current_state.theta.first = angle1_offset * 3.14159 / 180.0;
      current_state.theta.second = angle2_offset * 3.14159 / 180.0;
      current_state.omega.first = 0.0;
      current_state.omega.second = 0.0;
      sim_time = 0.0;
    }

    if (arm_evolution_setting) {
      if (arm_evolution_direction > 0) {
        arm1_length_target = arm1_length_max;
        arm2_length_target = arm2_length_max;
      } else {
        arm1_length_target = arm1_length_min;
        arm2_length_target = arm2_length_min;
      }
      float diff1 = arm1_length_target - current_arm1_length;
      float diff2 = arm2_length_target - current_arm2_length;
      if (fabsf(diff1) < 0.1f && fabsf(diff2) < 0.1f) {
        arm_evolution_direction *= -1;
      } else {
        current_arm1_length += diff1 * arm_evolution_speed;
        current_arm2_length += diff2 * arm_evolution_speed;
      }
      pendulum_system.length.first = current_arm1_length / 10.0f;
      pendulum_system.length.second = current_arm2_length / 10.0f;
    }

    current_state =
        dp::advance(current_state, pendulum_system, current_pendulum_speed);
    sim_time += 0.01;

    double x1 = PIVOT_X + current_arm1_length * sin(current_state.theta.first);
    double y1 = PIVOT_Y + current_arm1_length * cos(current_state.theta.first);

    double x2 = x1 + current_arm2_length * sin(current_state.theta.second);
    double y2 = y1 + current_arm2_length * cos(current_state.theta.second);

    int ix1 = (int)(x1 + 0.5);
    int iy1 = (int)(y1 + 0.5);
    int ix2 = (int)(x2 + 0.5);
    int iy2 = (int)(y2 + 0.5);

    display_clear();

    MatrixPanel_I2S_DMA* matrix = display_get_matrix();
    if (!matrix) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // Draw trail as individual pixels (smoother curve than connecting lines)
    for (int i = 0; i < current_trail_length; i++) {
      int idx = (trail_head - i - 1 + TRAIL_LENGTH_MAX) % TRAIL_LENGTH_MAX;
      if (trail_x[idx] >= 0 && trail_y[idx] >= 0) {
        uint8_t alpha;
        if (current_trail_length >= 2000 || current_trail_length < 0) {
          alpha = 255;  // "infinite" trail - no fade
        } else {
          alpha = (uint8_t)((current_trail_length - i) * 200 / current_trail_length);
        }
        uint8_t r, g, b;
        uint8_t hue = current_trail_color_cycle ? (hue_counter - i * 2) & 0xFF
                                                : trail_hue[idx];
        hsv_to_rgb(hue, 255, alpha, &r, &g, &b);
        // Apply brightness
        r = r * current_brightness / 255;
        g = g * current_brightness / 255;
        b = b * current_brightness / 255;
        if (trail_x[idx] >= 0 && trail_x[idx] < DISPLAY_WIDTH &&
            trail_y[idx] >= 0 && trail_y[idx] < DISPLAY_HEIGHT) {
          matrix->drawPixelRGB888(trail_x[idx], trail_y[idx], r, g, b);
        }
      }
    }

    // Update trail
    trail_x[trail_head] = ix2;
    trail_y[trail_head] = iy2;
    trail_hue[trail_head] = hue_counter;
    trail_head = (trail_head + 1) % TRAIL_LENGTH_MAX;

    // Apply brightness to leg color
    uint8_t leg_r =
        ((current_leg_color >> 16) & 0xFF) * current_brightness / 255;
    uint8_t leg_g =
        ((current_leg_color >> 8) & 0xFF) * current_brightness / 255;
    uint8_t leg_b = (current_leg_color & 0xFF) * current_brightness / 255;

    draw_line(PIVOT_X, PIVOT_Y, ix1, iy1, leg_r, leg_g, leg_b);
    draw_line(ix1, iy1, ix2, iy2, leg_r, leg_g, leg_b);

    // Top bob - smaller (2x2 pixels instead of 3x3)
    uint8_t top_r = 0 * current_brightness / 255;
    uint8_t top_g = 255 * current_brightness / 255;
    uint8_t top_b = 255 * current_brightness / 255;
    if (ix1 >= 0 && ix1 < DISPLAY_WIDTH && iy1 >= 0 && iy1 < DISPLAY_HEIGHT) {
      matrix->drawPixelRGB888(ix1, iy1, top_r, top_g, top_b);
    }
    if (ix1 + 1 >= 0 && ix1 + 1 < DISPLAY_WIDTH && iy1 >= 0 &&
        iy1 < DISPLAY_HEIGHT) {
      matrix->drawPixelRGB888(ix1 + 1, iy1, top_r, top_g, top_b);
    }
    if (ix1 >= 0 && ix1 < DISPLAY_WIDTH && iy1 + 1 >= 0 &&
        iy1 + 1 < DISPLAY_HEIGHT) {
      matrix->drawPixelRGB888(ix1, iy1 + 1, top_r, top_g, top_b);
    }
    if (ix1 + 1 >= 0 && ix1 + 1 < DISPLAY_WIDTH && iy1 + 1 >= 0 &&
        iy1 + 1 < DISPLAY_HEIGHT) {
      matrix->drawPixelRGB888(ix1 + 1, iy1 + 1, top_r, top_g, top_b);
    }

    uint8_t r, g, b;
    hsv_to_rgb(hue_counter, 255, 255, &r, &g, &b);
    // Apply brightness to bottom bob
    r = r * current_brightness / 255;
    g = g * current_brightness / 255;
    b = b * current_brightness / 255;
    draw_bob(ix2, iy2, r, g, b);

    hue_counter++;
    if (hue_counter == 0) {
      hue_counter = 1;
    }

    display_flip();

    vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
  }
}
