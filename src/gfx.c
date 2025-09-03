#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <string.h>
#include <webp/demux.h>

#include "display.h"
#include "esp_timer.h"
#include "lib/assets/assets.h"

static const char *TAG = "gfx";

#define GFX_TASK_CORE 1
#define GFX_TASK_PRIO 2
#define GFX_TASK_STACK_SIZE 4092

struct gfx_state {
  TaskHandle_t task;
  SemaphoreHandle_t mutex;
  void *buf;
  size_t len;
  int32_t dwell_secs;
  int counter;
};

static struct gfx_state *_state = NULL;

static void gfx_loop(void *arg);
static int draw_webp(uint8_t *buf, size_t len, int32_t dwell_secs, int32_t *isAnimating);

int gfx_initialize() {
  // Only initialize once
  if (_state) {
    ESP_LOGE(TAG, "Already initialized");
    return 1;
  }

  int heapl = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

  ESP_LOGI(TAG, "largest heap %d", heapl);
  // ESP_LOGI(TAG, "calling calloc");
  // Initialize state
  ESP_LOGI(TAG, "Allocating buffer of size: %d", ASSET_BOOT_WEBP_LEN);

  _state = calloc(1, sizeof(struct gfx_state));
  _state->len = ASSET_BOOT_WEBP_LEN;
  ESP_LOGI(TAG,"calloc buff");
  _state->buf = calloc(1, ASSET_BOOT_WEBP_LEN);
  ESP_LOGI(TAG, "done calloc, copying");
  if (_state->buf == NULL) {
    ESP_LOGE("gfx", "Memory allocation failed!");
    return 1;
  }
  memcpy(_state->buf, ASSET_BOOT_WEBP, ASSET_BOOT_WEBP_LEN);
  ESP_LOGI(TAG, "done, copying");
  
  _state->mutex = xSemaphoreCreateMutex();
  if (_state->mutex == NULL) {
    ESP_LOGE(TAG, "Could not create gfx mutex");
    return 1;
  }
  ESP_LOGI(TAG,"done with gfx init");

  // Initialize the display
  if (display_initialize()) {
    return 1;
  }

  // Launch the graphics loop in separate task
  BaseType_t ret =
      xTaskCreatePinnedToCore(gfx_loop,              // pvTaskCode
                              "gfx_loop",            // pcName
                              GFX_TASK_STACK_SIZE,   // usStackDepth
                              (void *)&isAnimating,  // pvParameters
                              GFX_TASK_PRIO,         // uxPriority
                              &_state->task,         // pxCreatedTask
                              GFX_TASK_CORE          // xCoreID
      );
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Could not create gfx task");
    return 1;
  }

  return 0;
}
int gfx_update(void *webp, size_t len, int32_t dwell_secs) {
  if (pdTRUE != xSemaphoreTake(_state->mutex, portMAX_DELAY)) {
    ESP_LOGE(TAG, "Could not take gfx mutex");
    return 1;
  }

  // If a new frame arrives before the previous one is consumed by the gfx task,
  // free the old buffer here to prevent a memory leak (frame-dropping strategy).
  if (_state->buf) {
    free(_state->buf);
    _state->buf = NULL;
  }

  // Take ownership of new buffer (no copy)
  _state->buf = webp;
  _state->len = len;
  _state->dwell_secs = dwell_secs;
  _state->counter++;

  if (pdTRUE != xSemaphoreGive(_state->mutex)) {
    ESP_LOGE(TAG, "Could not give gfx mutex");
    return 1;
  }

  return 0;
}

int gfx_display_asset(const char* asset_type) {
  const uint8_t* asset_data = NULL;
  size_t asset_len = 0;

  // Determine which asset to display
  if (strcmp(asset_type, "config") == 0) {
    asset_data = ASSET_CONFIG_WEBP;
    asset_len = ASSET_CONFIG_WEBP_LEN;
  } else if (strcmp(asset_type, "error_404") == 0) {
    asset_data = ASSET_404_WEBP;
    asset_len = ASSET_404_WEBP_LEN;
  } else if (strcmp(asset_type, "no_connect") == 0) {
    asset_data = ASSET_NOCONNECT_WEBP;
    asset_len = ASSET_NOCONNECT_WEBP_LEN;
  } else if (strcmp(asset_type, "oversize") == 0) {
    ESP_LOGI(TAG, "DISPLAYING OVERSIZE GRAPHIC");
    asset_data = ASSET_OVERSIZE_WEBP;
    asset_len = ASSET_OVERSIZE_WEBP_LEN;
  } else {
    ESP_LOGE(TAG, "Unknown asset type: %s", asset_type);
    return 1;
  }

  // Allocate heap memory and copy asset data
  uint8_t *asset_heap_copy = (uint8_t *)malloc(asset_len);
  if (asset_heap_copy == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for %s asset copy", asset_type);
    return 1;
  }

  memcpy(asset_heap_copy, asset_data, asset_len);

  // Display the asset with no dwell time (static display)
  int result = gfx_update(asset_heap_copy, asset_len, 0);
  if (result != 0) {
    ESP_LOGE(TAG, "Failed to update graphics with %s asset", asset_type);
    free(asset_heap_copy);
    return 1;
  }

  // gfx_update now owns the asset_heap_copy buffer
  return 0;
}

void gfx_shutdown() { display_shutdown(); }

static void gfx_loop(void *args) {
  void *webp = NULL;
  size_t len = 0;
  int32_t dwell_secs = 0;
  int counter = -1;
  int32_t *isAnimating = (int32_t *)args;
  ESP_LOGI(TAG, "Graphics loop running on core %d", xPortGetCoreID());

  for (;;) {
    if (pdTRUE != xSemaphoreTake(_state->mutex, portMAX_DELAY)) {
      ESP_LOGE(TAG, "Could not take gfx mutex");
      if (webp) {
        free(webp);
        webp = NULL;
      }
      break;
    }

    // If there's new data, take ownership of buffer
    if (counter != _state->counter) {
      ESP_LOGI(TAG, "Loaded new webp");
      if (webp) free(webp);
      webp = _state->buf;
      len = _state->len;
      dwell_secs = _state->dwell_secs;
      _state->buf = NULL; // gfx_loop now owns the buffer
      counter = _state->counter;
      if (*isAnimating == -1) *isAnimating = 1;
    }

    if (pdTRUE != xSemaphoreGive(_state->mutex)) {
      ESP_LOGE(TAG, "Could not give gfx mutex");
      continue;
    }

    if (webp && len > 0) {
      if (draw_webp(webp, len, dwell_secs, isAnimating)) {
        ESP_LOGE(TAG, "Could not draw webp");
        vTaskDelay(pdMS_TO_TICKS(1 * 1000));
        *isAnimating = 0;
        // Free the invalid buffer to prevent re-drawing it
        free(webp);
        webp = NULL;
        len = 0;
      }
      // keep webp around to loop until the next image arrives
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

static int draw_webp(uint8_t *buf, size_t len, int32_t dwell_secs, int32_t *isAnimating) {
  // Set up WebP decoder
  // ESP_LOGI(TAG, "starting draw_webp");
  int app_dwell_secs = dwell_secs;

  
  int64_t dwell_us;
  
  if (app_dwell_secs <= 0 ) {
    ESP_LOGW(TAG,"dwell_secs is 0. Looping one more time while we wait.");
    dwell_us = 1 * 1000000; // default to 1s if it's zero so we loop again or show the image for 1 more second.
  } else {
    ESP_LOGI(TAG, "dwell_secs : %d", app_dwell_secs);
    dwell_us = app_dwell_secs * 1000000;
  }
  // ESP_LOGI(TAG, "frame count: %d", animation.frame_count);
  
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
  // ESP_LOGI(TAG, "frame count: %d", animation.frame_count);
  int64_t start_us = esp_timer_get_time();
  while (esp_timer_get_time() - start_us < dwell_us) {
    int lastTimestamp = 0;
    int delay = 0;
    TickType_t drawStartTick = xTaskGetTickCount();

    // Draw each frame, and sleep for the delay
    while (WebPAnimDecoderHasMoreFrames(decoder) && *isAnimating != -1) {
      uint8_t *pix;
      int timestamp;
      WebPAnimDecoderGetNext(decoder, &pix, &timestamp);
      if (delay > 0) {
        xTaskDelayUntil(&drawStartTick, pdMS_TO_TICKS(delay));
      } else {
        vTaskDelay(10); // small delay for yield.
      }
      drawStartTick = xTaskGetTickCount();
      display_draw(pix, animation.canvas_width, animation.canvas_height, 4, 0,
                   1, 2);
      delay = timestamp - lastTimestamp;
      lastTimestamp = timestamp;
    }
  
    // reset decoder to start from the beginning
    WebPAnimDecoderReset(decoder);
    
    if (delay > 0) {
      xTaskDelayUntil(&drawStartTick, pdMS_TO_TICKS(delay));
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));  // Add a small fallback delay to yield CPU
    }
    
    // In case of a single frame, sleep for app_dwell_secs
    if (animation.frame_count == 1) {
      // ESP_LOGI(TAG, "single frame delay for %d", app_dwell_secs);
      // xTaskDelayUntil(&start_us, dwell_us);
      vTaskDelay(pdMS_TO_TICKS(dwell_us / 1000));  // full dwell delay 
      // *isAnimating = 0;
      break;
    }
  }
  WebPAnimDecoderDelete(decoder);

  ESP_LOGI(TAG, "Setting isAnimating to 0");
  *isAnimating = 0;
  return 0;
}
