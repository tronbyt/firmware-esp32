/**
 * touch_control.h
 *
 * Touch control interface for Tidbyt Gen 2
 * Single touch zone on GPIO33 (TOUCH_PAD_NUM8)
 */

#ifndef TOUCH_CONTROL_H
#define TOUCH_CONTROL_H

#include "driver/touch_pad.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Touch pad assignment for Tidbyt Gen 2
#define TOUCH_PAD_MAIN TOUCH_PAD_NUM8  // GPIO33

// Touch threshold - based on ESPHome Tidbyt Gen2 config
// Untouched values are typically 900-1000, touched drops below this
// ESPHome uses 1200 as threshold (touch triggers when value < 1200)
// But we'll use adaptive calibration - this is just the starting point
#define TOUCH_THRESHOLD_DEFAULT 1200

// Debounce time (250ms matches official Tidbyt HDK)
#define TOUCH_DEBOUNCE_MS 250

// Debug logging - set to 1 to see touch values in serial monitor
#define TOUCH_DEBUG_ENABLED 1

typedef enum {
  TOUCH_EVENT_NONE = 0,
  TOUCH_EVENT_TAP,         // Single tap - next app
  TOUCH_EVENT_DOUBLE_TAP,  // Double tap - cycle brightness
  TOUCH_EVENT_HOLD         // Long hold (2+ sec) - toggle display on/off
} touch_event_t;

esp_err_t touch_control_init(void);
touch_event_t touch_control_check(void);
void touch_control_set_threshold(uint16_t threshold);
uint16_t touch_control_get_threshold(void);
void touch_control_set_debounce(uint32_t ms);
void touch_control_calibrate(void);
void touch_control_debug_all_pads(void);
uint16_t touch_control_read_raw(touch_pad_t pad);
bool touch_control_is_initialized(void);
const char* touch_event_to_string(touch_event_t event);

#ifdef __cplusplus
}
#endif

#endif  // TOUCH_CONTROL_H
