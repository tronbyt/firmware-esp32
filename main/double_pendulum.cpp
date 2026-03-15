#include "double_pendulum.hpp"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
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

#define FRAME_DELAY_MS 40

static dp::state current_state;
static dp::system pendulum_system;
static double sim_time = 0.0;
static uint8_t hue_counter = 0;

static SemaphoreHandle_t frame_sem;
static volatile bool new_frame_ready = false;

static int bob1_x, bob1_y, bob2_x, bob2_y;
static uint8_t bob2_r, bob2_g, bob2_b;

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
      display_draw_pixel(x0, y0, r, g, b);
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
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      int px = x + dx;
      int py = y + dy;
      if (px >= 0 && px < DISPLAY_WIDTH && py >= 0 && py < DISPLAY_HEIGHT) {
        display_draw_pixel(px, py, r, g, b);
      }
    }
  }
}

static void compute_task(void* param) {
  (void)param;

  while (1) {
    for (int i = 0; i < 2; i++) {
      current_state = dp::advance(current_state, pendulum_system, 0.01);
      sim_time += 0.01;
    }

    double x1 = PIVOT_X + ARM1_LENGTH * sin(current_state.theta.first);
    double y1 = PIVOT_Y + ARM1_LENGTH * cos(current_state.theta.first);

    double x2 = x1 + ARM2_LENGTH * sin(current_state.theta.second);
    double y2 = y1 + ARM2_LENGTH * cos(current_state.theta.second);

    bob1_x = (int)(x1 + 0.5);
    bob1_y = (int)(y1 + 0.5);
    bob2_x = (int)(x2 + 0.5);
    bob2_y = (int)(y2 + 0.5);

    hsv_to_rgb(hue_counter, 255, 255, &bob2_r, &bob2_g, &bob2_b);

    hue_counter++;
    if (hue_counter == 0) {
      hue_counter = 1;
    }

    new_frame_ready = true;
    xSemaphoreGive(frame_sem);

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void display_task(void* param) {
  (void)param;

  while (1) {
    xSemaphoreTake(frame_sem, portMAX_DELAY);

    display_clear();

    draw_line(PIVOT_X, PIVOT_Y, bob1_x, bob1_y, 255, 255, 255);
    draw_line(bob1_x, bob1_y, bob2_x, bob2_y, 255, 255, 255);

    draw_bob(bob1_x, bob1_y, 0, 255, 255);
    draw_bob(bob2_x, bob2_y, bob2_r, bob2_g, bob2_b);

    display_flip();

    vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
  }
}

void dp_init(void) {
  ESP_LOGI(TAG, "Initializing double pendulum");

  pendulum_system.mass.first = 1.0;
  pendulum_system.mass.second = 1.0;
  pendulum_system.length.first = 1.0;
  pendulum_system.length.second = 1.0;

  current_state.theta.first = 170.0 * 3.14159 / 180.0;
  current_state.theta.second = 150.0 * 3.14159 / 180.0;
  current_state.omega.first = 0.0;
  current_state.omega.second = 0.0;

  sim_time = 0.0;
  hue_counter = 0;

  ESP_LOGI(TAG, "Initial angles: theta1=%.2f rad, theta2=%.2f rad",
           current_state.theta.first, current_state.theta.second);
}

static TaskHandle_t compute_handle = NULL;
static TaskHandle_t display_handle = NULL;

void dp_run(void) {
  ESP_LOGI(TAG, "Starting double pendulum animation");

  dp_init();

  frame_sem = xSemaphoreCreateBinary();

  xTaskCreatePinnedToCore(compute_task, "pendulum_compute", 4096, NULL, 5,
                          &compute_handle, 0);
  xTaskCreatePinnedToCore(display_task, "pendulum_display", 4096, NULL, 3,
                          &display_handle, 1);

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
