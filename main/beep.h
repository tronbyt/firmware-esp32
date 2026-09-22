/**
 * beep.h
 *
 * Touch feedback tone for Tidbyt Gen 2 (I2S amplifier on GPIO 12/13/14)
 */

#ifndef BEEP_H
#define BEEP_H

#ifdef __cplusplus
extern "C" {
#endif

// Short tone for a tap, lower and longer one for a hold
typedef enum {
  BEEP_TAP = 0,
  BEEP_HOLD,
} beep_kind_t;

// Queue a tone and return immediately. The I2S peripheral is only set up on
// the first call, so a device that is never touched never runs this code.
void beep_play(beep_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif  // BEEP_H
