/**
 * beep.c
 *
 * Touch feedback tone for Tidbyt Gen 2.
 *
 * The speaker amplifier sits on I2S: BCLK=GPIO12, LRCLK=GPIO13, DOUT=GPIO14,
 * no MCLK and no enable pin (same pinout as the Tidbyt HDK). The HUB75 driver
 * owns I2S1, so audio uses I2S0.
 */

#include "beep.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "beep";

#define BEEP_SAMPLE_RATE 16000
#define BEEP_AMPLITUDE 5000  // out of 32767: a small speaker needs little
#define BEEP_ATTACK_MS 3     // ramps at both ends keep the amp from popping
#define BEEP_PAD_MS 20       // silence around the tone, flushes the DMA

typedef struct {
  uint16_t hz;
  uint16_t ms;
} tone_t;

static const tone_t TONES[] = {
    [BEEP_TAP] = {.hz = 2000, .ms = 40},
    [BEEP_HOLD] = {.hz = 1000, .ms = 120},
};

static TaskHandle_t s_task = NULL;
static i2s_chan_handle_t s_tx = NULL;
static bool s_failed = false;

static esp_err_t i2s_setup(void) {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  // HUB75 already takes most of the DMA-capable RAM
  chan_cfg.dma_desc_num = 4;
  chan_cfg.dma_frame_num = 256;

  esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
  if (err != ESP_OK) {
    return err;
  }

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BEEP_SAMPLE_RATE),
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                  I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = GPIO_NUM_12,
              .ws = GPIO_NUM_13,
              .dout = GPIO_NUM_14,
              .din = I2S_GPIO_UNUSED,
          },
  };
  err = i2s_channel_init_std_mode(s_tx, &std_cfg);
  if (err != ESP_OK) {
    i2s_del_channel(s_tx);
    s_tx = NULL;
  }
  return err;
}

static void play(const tone_t* tone) {
  const int pad = BEEP_SAMPLE_RATE * BEEP_PAD_MS / 1000;
  const int body = BEEP_SAMPLE_RATE * tone->ms / 1000;
  const int attack = BEEP_SAMPLE_RATE * BEEP_ATTACK_MS / 1000;
  const int frames = pad + body + pad;

  // stereo frames, zeroed: the padding stays silent
  int16_t* buf = calloc(frames * 2, sizeof(int16_t));
  if (buf == NULL) {
    ESP_LOGW(TAG, "No memory for tone");
    return;
  }

  for (int i = 0; i < body; i++) {
    float envelope = 1.0f - (float)i / body;  // linear decay to zero
    if (i < attack) {
      envelope *= (float)i / attack;
    }
    float phase = 2.0f * (float)M_PI * tone->hz * i / BEEP_SAMPLE_RATE;
    int16_t sample = (int16_t)(BEEP_AMPLITUDE * envelope * sinf(phase));
    buf[(pad + i) * 2] = sample;
    buf[(pad + i) * 2 + 1] = sample;
  }

  // The clocks only run while a tone plays: the amp idles without BCLK
  if (i2s_channel_enable(s_tx) == ESP_OK) {
    size_t written = 0;
    i2s_channel_write(s_tx, buf, frames * 2 * sizeof(int16_t), &written,
                      pdMS_TO_TICKS(500));
    // let the DMA drain what it buffered before stopping the clocks
    vTaskDelay(pdMS_TO_TICKS(BEEP_PAD_MS));
    i2s_channel_disable(s_tx);
  }
  free(buf);
}

static void beep_task(void* arg) {
  (void)arg;
  while (true) {
    uint32_t kind = 0;
    xTaskNotifyWait(0, UINT32_MAX, &kind, portMAX_DELAY);

    if (s_tx == NULL && !s_failed) {
      esp_err_t err = i2s_setup();
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S setup failed: %s (touch stays silent)",
                 esp_err_to_name(err));
        s_failed = true;
      }
    }
    if (s_tx != NULL && kind < sizeof(TONES) / sizeof(TONES[0])) {
      play(&TONES[kind]);
    }
  }
}

void beep_play(beep_kind_t kind) {
  if (s_task == NULL) {
    if (xTaskCreate(beep_task, "beep", 3072, NULL, 3, &s_task) != pdPASS) {
      s_task = NULL;
      return;
    }
  }
  xTaskNotify(s_task, (uint32_t)kind, eSetValueWithOverwrite);
}
