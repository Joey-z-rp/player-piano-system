#include "key_driver.h"

// Global key driver instance
KeyDriverModule_t g_key_driver;

// Initialize the key driver module
void KeyDriver_Init(KeyDriverModule_t *key_driver)
{
  if (key_driver == NULL)
  {
    return;
  }

  // Initialize all keys to idle state
  for (uint8_t i = 0; i < NUM_KEYS; i++)
  {
    key_driver->keys[i].state = KEY_STATE_IDLE;
    key_driver->keys[i].initial_strike_start_time = 0;
    key_driver->keys[i].initial_duty_cycle = 0;
    key_driver->keys[i].hold_duty_cycle = HOLD_DUTY_CYCLE;
  }

  // Set global hold duty cycle
  key_driver->hold_duty_cycle = HOLD_DUTY_CYCLE;
}

// Press a key with specified duty cycle
void KeyDriver_PressKey(KeyDriverModule_t *key_driver, uint8_t key, uint8_t duty_cycle)
{
  if (key_driver == NULL || key >= NUM_KEYS)
  {
    return;
  }

  // Clamp duty cycle to valid range (0-100)
  if (duty_cycle > 100)
  {
    duty_cycle = 100;
  }

  // Set key state to initial strike
  key_driver->keys[key].state = KEY_STATE_INITIAL_STRIKE;
  key_driver->keys[key].initial_strike_start_time = HAL_GetTick();
  key_driver->keys[key].initial_duty_cycle = duty_cycle;
  key_driver->keys[key].hold_duty_cycle = HOLD_DUTY_CYCLE;

  // Immediately set the initial duty cycle
  PWM_SetDutyCycle(key, duty_cycle);
}

// Release a key (set duty cycle to 0)
void KeyDriver_ReleaseKey(KeyDriverModule_t *key_driver, uint8_t key)
{
  if (key_driver == NULL || key >= NUM_KEYS)
  {
    return;
  }

  // Set key state to idle
  key_driver->keys[key].state = KEY_STATE_IDLE;

  // Set duty cycle to 0
  PWM_SetDutyCycle(key, 0);
}

// Update key driver state machine (call this in main loop)
void KeyDriver_Update(KeyDriverModule_t *key_driver)
{
  if (key_driver == NULL)
  {
    return;
  }

  uint32_t current_time = HAL_GetTick();

  for (uint8_t i = 0; i < NUM_KEYS; i++)
  {
    KeyDriver_t *key = &key_driver->keys[i];

    switch (key->state)
    {
    case KEY_STATE_IDLE:
      // Nothing to do in idle state
      break;

    case KEY_STATE_INITIAL_STRIKE:
      // Check if initial strike time has elapsed
      if ((current_time - key->initial_strike_start_time) >= INITIAL_STRIKE_TIME_MS)
      {
        // Transition to hold state
        key->state = KEY_STATE_HOLD;
        PWM_SetDutyCycle(i, key->hold_duty_cycle);
      }
      break;

    case KEY_STATE_HOLD:
      // Continue holding at hold duty cycle
      // This state is maintained until key is released
      break;
    }
  }
}
