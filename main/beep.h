/**
 * beep.h
 *
 * Touch feedback tone for Tidbyt Gen 2 (I2S amplifier on GPIO 12/13/14)
 */

#ifndef BEEP_H
#define BEEP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Short tone for a tap, lower and longer one for a hold
typedef enum {
  BEEP_TAP = 0,
  BEEP_HOLD,
} beep_kind_t;

// Initialize the beep subsystem. Safe to call multiple times.
void beep_init(void);

// Set default speaker volume (0 to 100%)
void beep_set_volume(uint8_t volume);

// Get current speaker volume (0 to 100%)
uint8_t beep_get_volume(void);

// Queue a tone and return immediately. The I2S peripheral is only set up on
// the first call, so a device that is never touched never runs this code.
void beep_play(beep_kind_t kind);

// Queue a tone sequence (comma-separated "hz:ms" pairs, e.g. "523:120,659:120,784:220")
void beep_play_sequence(const char* pattern);

// Queue a tone sequence with an explicit volume (0 to 100%)
void beep_play_sequence_with_volume(const char* pattern, uint8_t volume);

// Queue the Tron/Bit inspired startup jingle
void beep_play_startup(void);

#ifdef __cplusplus
}
#endif

#endif  // BEEP_H
