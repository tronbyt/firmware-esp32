#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>
#include <webp/demux.h>

#include "display.h"

static const char *TAG = "gfx";

#define GFX_TASK_CORE 1
#define GFX_TASK_PRIO 2
#define GFX_TASK_STACK_SIZE 4092

struct gfx_state {
  TaskHandle_t task;
  SemaphoreHandle_t mutex;
  void *buf;
  size_t len;
  int counter;
};

static struct gfx_state *_state = NULL;

static void gfx_loop(void *arg);
static int draw_webp(uint8_t *webp, size_t len,int *p);

int gfx_initialize(const void *webp, size_t len) {
  // Only initialize once
  if (_state) {
    ESP_LOGE(TAG, "Already initialized");
    return 1;
  }

  // Initialize state
  _state = calloc(1, sizeof(struct gfx_state));
  _state->len = len;
  _state->buf = calloc(1, len);
  memcpy(_state->buf, webp, len);

  _state->mutex = xSemaphoreCreateMutex();
  if (_state->mutex == NULL) {
    ESP_LOGE(TAG, "Could not create gfx mutex");
    return 1;
  }

  // Initialize the display
  if (display_initialize()) {
    return 1;
  }

  // Launch the graphics loop in separate task
  BaseType_t ret = xTaskCreatePinnedToCore(gfx_loop,             // pvTaskCode
                                           "gfx_loop",           // pcName
                                           GFX_TASK_STACK_SIZE,  // usStackDepth
                                           (void*)&isAnimating,                 // pvParameters
                                           GFX_TASK_PRIO,        // uxPriority
                                           &_state->task,  // pxCreatedTask
                                           GFX_TASK_CORE   // xCoreID
  );
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Could not create gfx task");
    return 1;
  }

  return 0;
}

int gfx_update(const void *webp, size_t len) {
  // Take mutex
  if (pdTRUE != xSemaphoreTake(_state->mutex, portMAX_DELAY)) {
    ESP_LOGE(TAG, "Could not take gfx mutex");
    return 1;
  }

  // Update state
  if (len > _state->len) {
    // Free the old buffer only if it exists
    if (_state->buf) {
      free(_state->buf);
      _state->buf = NULL;  // Set to NULL to avoid dangling pointers
    }

    // Allocate new memory
    _state->buf = malloc(len);
    if (!_state->buf) {
      ESP_LOGE("main", "Failed to allocate memory for _state->buf");
      return;  // Exit early to avoid using NULL buffer
    }

    _state->len = len;  // Update length after successful allocation
  }

  // Copy data to buffer
  if (_state->buf && webp) {
    memcpy(_state->buf, webp, len);
    _state->counter++;
  } else {
    ESP_LOGE("main", "Buffer or input data is NULL");
  }

  // Give mutex
  if (pdTRUE != xSemaphoreGive(_state->mutex)) {
    ESP_LOGE(TAG, "Could not give gfx mutex");
    return 1;
  }

  return 0;
}

void gfx_shutdown() { display_shutdown(); }

static void gfx_loop(void *args) {
  void *webp = NULL;
  size_t len = 0;
  int counter = -1;
  int32_t *p = (int32_t *)args;  // Cast to pointer type
  ESP_LOGI(TAG, "Graphics loop running on core %d", xPortGetCoreID());

  for (;;) {
    // Take mutex
    if (pdTRUE != xSemaphoreTake(_state->mutex, portMAX_DELAY)) {
      ESP_LOGE(TAG, "Could not take gfx mutex");
      break;
    }

    // If there's new data, copy it to local buffer
    if (counter != _state->counter) {
      if (_state->len > len) {
        free(webp);
        webp = malloc(_state->len);
      }
      len = _state->len;
      counter = _state->counter;
      memcpy(webp, _state->buf, _state->len);
    }

    // Give mutex
    if (pdTRUE != xSemaphoreGive(_state->mutex)) {
      ESP_LOGE(TAG, "Could not give gfx mutex");
      continue;
    }

    // Draw it
    if (draw_webp(webp, len, p)) {
      ESP_LOGE(TAG, "Could not draw webp");
      vTaskDelay(pdMS_TO_TICKS(1 * 1000));
    }
  }
}

static int draw_webp(uint8_t *buf, size_t len, int32_t *isAnimating) {
  // Set up WebP decoder
  WebPData webpData;
  WebPDataInit(&webpData);
  webpData.bytes = buf;
  webpData.size = len;

  WebPAnimDecoderOptions decoderOptions;
  WebPAnimDecoderOptionsInit(&decoderOptions);
  decoderOptions.color_mode = MODE_RGBA;

  WebPAnimDecoder *decoder = WebPAnimDecoderNew(&webpData, &decoderOptions);
  if (decoder == NULL) {
    ESP_LOGE(TAG, "Could not create WebP decoder");
    return 1;
  }

  WebPAnimInfo animation;
  if (!WebPAnimDecoderGetInfo(decoder, &animation)) {
    ESP_LOGE(TAG, "Could not get WebP animation");
    return 1;
  }

  int lastTimestamp = 0;
  int delay = 0;

  // Draw each frame, and sleep for the delay
  for (int j = 0; j < animation.frame_count; j++) {
    *isAnimating = 1;
    uint8_t *pix;
    int timestamp;
    WebPAnimDecoderGetNext(decoder, &pix, &timestamp);
    vTaskDelay(pdMS_TO_TICKS(delay));
    display_draw(pix, animation.canvas_width, animation.canvas_height, 4, 0, 1,
                 2);
    delay = timestamp - lastTimestamp;
    lastTimestamp = timestamp;
  }
  *isAnimating = 0;
  vTaskDelay(pdMS_TO_TICKS(delay));

  // In case of a single frame, sleep for 1s
  if (animation.frame_count == 1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  WebPAnimDecoderDelete(decoder);
  return 0;
}
