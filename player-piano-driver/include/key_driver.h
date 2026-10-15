#ifndef KEY_DRIVER_H
#define KEY_DRIVER_H

#include "stm32f1xx_hal.h"
#include "pwm_output_config.h"
#include "board_config.h"

// Key driver configuration. PWM hardware stays at 15 outputs;
// this board only drives the keys in its range.
#define NUM_KEYS (LOCAL_CHANNELS + PEDAL_LOCAL_CHANNELS)
_Static_assert(NUM_KEYS <= PWM_NUM_CHANNELS, "local channels exceed PWM outputs");
// Duty cycles are in hundredths of a percent (PWM_DUTY_FULL = 100%).
#define INITIAL_STRIKE_TIME_MS 50
#define HOLD_DUTY 2000      // 20%: held after the strike until the key is released
#define DUTY_OMITTED 0xFFFF // a hold duty the command left out: use HOLD_DUTY
#define MAX_PHASES 3        // phases a press can have between the strike and the hold

// Key state enumeration
typedef enum
{
  KEY_STATE_IDLE = 0,
  KEY_STATE_INITIAL_STRIKE,
  KEY_STATE_PHASE, // one of the phases between the strike and the hold
  KEY_STATE_HOLD
} KeyState_t;

// A press: the strike, then up to MAX_PHASES phases (e.g. a coast, then a
// clamp), then the hold until the release. A phase with a duty or time of 0
// is skipped.
typedef struct
{
  uint16_t duty_cycle;
  uint16_t time_ms;
} KeyPhase_t;

// Key driver structure for each key
typedef struct
{
  KeyState_t state;
  uint32_t initial_strike_start_time;
  uint32_t phase_start_time;
  uint16_t initial_duty_cycle;
  uint16_t hold_duty_cycle;
  uint16_t initial_strike_time_ms;
  KeyPhase_t phases[MAX_PHASES];
  uint8_t phase_count;
  uint8_t phase; // the phase playing, in KEY_STATE_PHASE
} KeyDriver_t;

// Key driver module structure
typedef struct
{
  KeyDriver_t keys[NUM_KEYS];
  uint16_t hold_duty_cycle;
} KeyDriverModule_t;

// Function declarations
void KeyDriver_Init(KeyDriverModule_t *key_driver);
void KeyDriver_PressKey(KeyDriverModule_t *key_driver, uint8_t key, uint16_t duty_cycle, uint16_t initial_strike_time,
                        const KeyPhase_t *phases, uint8_t phase_count, uint16_t hold_duty_cycle);
void KeyDriver_ReleaseKey(KeyDriverModule_t *key_driver, uint8_t key);
void KeyDriver_Update(KeyDriverModule_t *key_driver);

// External key driver instance
extern KeyDriverModule_t g_key_driver;

#endif // KEY_DRIVER_H
