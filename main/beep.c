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
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char* TAG = "beep";

#define BEEP_SAMPLE_RATE 16000
#define BEEP_AMPLITUDE 5000  // out of 32767: a small speaker needs little
#define BEEP_ATTACK_MS 3     // ramps at both ends keep the amp from popping
#define BEEP_PAD_MS 20       // silence around the tone, flushes the DMA
#define CHUNK_FRAMES 128

typedef struct {
  uint16_t hz;
  uint16_t ms;
} tone_t;

static const tone_t TONES[] = {
    [BEEP_TAP] = {.hz = 2000, .ms = 40},
    [BEEP_HOLD] = {.hz = 1000, .ms = 120},
};

typedef enum {
  BEEP_MSG_KIND = 0,
  BEEP_MSG_SEQUENCE,
} beep_msg_type_t;

typedef struct {
  beep_msg_type_t type;
  union {
    beep_kind_t kind;
    char sequence[128];
  };
} beep_msg_t;

static TaskHandle_t s_task = NULL;
static QueueHandle_t s_queue = NULL;
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

static void write_silence(int ms) {
  int total_frames = BEEP_SAMPLE_RATE * ms / 1000;
  int16_t zero_buf[CHUNK_FRAMES * 2] = {0};
  while (total_frames > 0) {
    int chunk = (total_frames > CHUNK_FRAMES) ? CHUNK_FRAMES : total_frames;
    size_t written = 0;
    i2s_channel_write(s_tx, zero_buf, chunk * 2 * sizeof(int16_t), &written,
                      pdMS_TO_TICKS(500));
    total_frames -= chunk;
  }
}

static void write_tone(uint16_t hz, int ms) {
  if (ms <= 0) return;
  const int total_frames = BEEP_SAMPLE_RATE * ms / 1000;
  if (total_frames <= 0) return;
  const int attack = BEEP_SAMPLE_RATE * BEEP_ATTACK_MS / 1000;

  int16_t buf[CHUNK_FRAMES * 2];
  int frame_idx = 0;
  while (frame_idx < total_frames) {
    int chunk = total_frames - frame_idx;
    if (chunk > CHUNK_FRAMES) {
      chunk = CHUNK_FRAMES;
    }
    for (int i = 0; i < chunk; i++) {
      int cur = frame_idx + i;
      float envelope = 1.0f - (float)cur / total_frames;
      if (cur < attack) {
        envelope *= (float)cur / attack;
      }
      float phase = 2.0f * (float)M_PI * hz * cur / BEEP_SAMPLE_RATE;
      int16_t sample = (int16_t)(BEEP_AMPLITUDE * envelope * sinf(phase));
      buf[i * 2] = sample;
      buf[i * 2 + 1] = sample;
    }
    size_t written = 0;
    i2s_channel_write(s_tx, buf, chunk * 2 * sizeof(int16_t), &written,
                      pdMS_TO_TICKS(500));
    frame_idx += chunk;
  }
}

static void write_pad(void) {
  write_silence(BEEP_PAD_MS);
}

static void play(const tone_t* tone) {
  if (i2s_channel_enable(s_tx) == ESP_OK) {
    write_pad();
    write_tone(tone->hz, tone->ms);
    write_pad();
    vTaskDelay(pdMS_TO_TICKS(BEEP_PAD_MS));
    i2s_channel_disable(s_tx);
  }
}

static void play_sequence(const char* pattern) {
  if (pattern == NULL || *pattern == '\0') {
    return;
  }
  char copy[128];
  strncpy(copy, pattern, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';

  if (i2s_channel_enable(s_tx) == ESP_OK) {
    write_pad();
    char* saveptr = NULL;
    char* token = strtok_r(copy, ",", &saveptr);
    while (token != NULL) {
      int hz = 0, ms = 0;
      if (sscanf(token, "%d:%d", &hz, &ms) == 2 && ms > 0) {
        // Clamp duration to prevent integer overflow and soft-lock
        if (ms > 10000) {
          ms = 10000;
        }
        if (hz <= 0) {
          write_silence(ms);
        } else {
          if (hz > 20000) {
            hz = 20000;
          }
          write_tone((uint16_t)hz, ms);
        }
      }
      token = strtok_r(NULL, ",", &saveptr);
    }
    write_pad();
    vTaskDelay(pdMS_TO_TICKS(BEEP_PAD_MS));
    i2s_channel_disable(s_tx);
  }
}

static void beep_task(void* arg) {
  (void)arg;
  beep_msg_t msg;
  while (true) {
    if (xQueueReceive(s_queue, &msg, portMAX_DELAY) != pdPASS) {
      continue;
    }

    if (s_tx == NULL && !s_failed) {
      esp_err_t err = i2s_setup();
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S setup failed: %s (audio stays silent)",
                 esp_err_to_name(err));
        s_failed = true;
      }
    }
    if (s_tx != NULL) {
      if (msg.type == BEEP_MSG_KIND) {
        if (msg.kind < sizeof(TONES) / sizeof(TONES[0])) {
          play(&TONES[msg.kind]);
        }
      } else if (msg.type == BEEP_MSG_SEQUENCE) {
        play_sequence(msg.sequence);
      }
    }
  }
}

static portMUX_TYPE s_beep_mux = portMUX_INITIALIZER_UNLOCKED;

void beep_init(void) {
  taskENTER_CRITICAL(&s_beep_mux);
  if (s_queue == NULL) {
    s_queue = xQueueCreate(4, sizeof(beep_msg_t));
  }
  if (s_task == NULL && s_queue != NULL) {
    xTaskCreate(beep_task, "beep", 4096, NULL, 3, &s_task);
  }
  taskEXIT_CRITICAL(&s_beep_mux);
}

void beep_play(beep_kind_t kind) {
  if (s_queue == NULL) {
    beep_init();
    if (s_queue == NULL) {
      return;
    }
  }
  beep_msg_t msg = {
      .type = BEEP_MSG_KIND,
      .kind = kind,
  };
  xQueueSend(s_queue, &msg, 0);
}

void beep_play_sequence(const char* pattern) {
  if (pattern == NULL || *pattern == '\0') {
    return;
  }
  if (s_queue == NULL) {
    beep_init();
    if (s_queue == NULL) {
      return;
    }
  }
  beep_msg_t msg = {
      .type = BEEP_MSG_SEQUENCE,
  };
  strncpy(msg.sequence, pattern, sizeof(msg.sequence) - 1);
  msg.sequence[sizeof(msg.sequence) - 1] = '\0';
  xQueueSend(s_queue, &msg, 0);
}

#define BEEP_STARTUP_JINGLE \
  "1047:40,0:15,1319:40,0:15,1568:40,0:15,2093:60,0:20,1568:50,0:15,2093:150"

void beep_play_startup(void) {
  beep_play_sequence(BEEP_STARTUP_JINGLE);
}
