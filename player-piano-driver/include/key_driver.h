#ifndef KEY_DRIVER_H
#define KEY_DRIVER_H

#include "stm32f1xx_hal.h"
#include "pwm_output_config.h"

// Key driver configuration
#define NUM_KEYS 12
#define INITIAL_STRIKE_TIME_MS 50
#define HOLD_DUTY_CYCLE 20
#define MAX_VELOCITY 127
#define MIN_VELOCITY 0

// Key state enumeration
typedef enum
{
  KEY_STATE_IDLE = 0,
  KEY_STATE_INITIAL_STRIKE,
  KEY_STATE_HOLD
} KeyState_t;

// Key driver structure for each key
typedef struct
{
  KeyState_t state;
  uint32_t initial_strike_start_time;
  uint8_t initial_duty_cycle;
  uint8_t hold_duty_cycle;
} KeyDriver_t;

// Key driver module structure
typedef struct
{
  KeyDriver_t keys[NUM_KEYS];
  uint8_t hold_duty_cycle;
} KeyDriverModule_t;

// Function declarations
void KeyDriver_Init(KeyDriverModule_t *key_driver);
void KeyDriver_PressKey(KeyDriverModule_t *key_driver, uint8_t key, uint8_t velocity);
void KeyDriver_ReleaseKey(KeyDriverModule_t *key_driver, uint8_t key);
void KeyDriver_Update(KeyDriverModule_t *key_driver);
uint8_t KeyDriver_MapVelocityToDutyCycle(uint8_t velocity);

// External key driver instance
extern KeyDriverModule_t g_key_driver;

#endif // KEY_DRIVER_H
