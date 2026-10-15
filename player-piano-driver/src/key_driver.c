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
    key_driver->keys[i].phase_start_time = 0;
    key_driver->keys[i].initial_duty_cycle = 0;
    key_driver->keys[i].hold_duty_cycle = HOLD_DUTY;
    key_driver->keys[i].initial_strike_time_ms = INITIAL_STRIKE_TIME_MS;
    key_driver->keys[i].phase_count = 0;
    key_driver->keys[i].phase = 0;
  }

  // Set global hold duty cycle
  key_driver->hold_duty_cycle = HOLD_DUTY;
}

// From phase `from` on, the first phase that has a duty and a time, or
// phase_count if none is left.
static uint8_t NextPhase(const KeyDriver_t *key, uint8_t from)
{
  while (from < key->phase_count && (key->phases[from].duty_cycle == 0 || key->phases[from].time_ms == 0))
  {
    from++;
  }
  return from;
}

// Starts phase `phase` at `now`, or the hold if no phase is left.
static void StartPhase(KeyDriver_t *key, uint8_t channel, uint8_t phase, uint32_t now)
{
  phase = NextPhase(key, phase);
  if (phase < key->phase_count)
  {
    key->state = KEY_STATE_PHASE;
    key->phase = phase;
    key->phase_start_time = now;
    PWM_SetDutyCycle(channel, key->phases[phase].duty_cycle);
  }
  else
  {
    key->state = KEY_STATE_HOLD;
    PWM_SetDutyCycle(channel, key->hold_duty_cycle);
  }
}

// Press a key: the strike, then its phases, then the hold until released.
void KeyDriver_PressKey(KeyDriverModule_t *key_driver, uint8_t key, uint16_t duty_cycle, uint16_t initial_strike_time,
                        const KeyPhase_t *phases, uint8_t phase_count, uint16_t hold_duty_cycle)
{
  if (key_driver == NULL || key >= NUM_KEYS)
  {
    return;
  }

  // Clamp duty cycles to 0-100%
  if (duty_cycle > PWM_DUTY_FULL)
  {
    duty_cycle = PWM_DUTY_FULL;
  }
  if (phase_count > MAX_PHASES)
  {
    phase_count = MAX_PHASES;
  }

  // 0-100% is explicit (including 0%); DUTY_OMITTED uses the default hold.
  if (hold_duty_cycle == DUTY_OMITTED)
  {
    hold_duty_cycle = HOLD_DUTY;
  }
  else if (hold_duty_cycle > PWM_DUTY_FULL)
  {
    hold_duty_cycle = PWM_DUTY_FULL;
  }

  // Set key state to initial strike
  key_driver->keys[key].state = KEY_STATE_INITIAL_STRIKE;
  key_driver->keys[key].initial_strike_start_time = HAL_GetTick();
  key_driver->keys[key].phase_start_time = 0;
  key_driver->keys[key].initial_duty_cycle = duty_cycle;
  key_driver->keys[key].hold_duty_cycle = hold_duty_cycle;
  for (uint8_t i = 0; i < phase_count; i++)
  {
    key_driver->keys[key].phases[i] = phases[i];
    if (key_driver->keys[key].phases[i].duty_cycle > PWM_DUTY_FULL)
    {
      key_driver->keys[key].phases[i].duty_cycle = PWM_DUTY_FULL;
    }
  }
  key_driver->keys[key].phase_count = phase_count;
  key_driver->keys[key].phase = 0;

  // Set timing parameters (use defaults if 0)
  key_driver->keys[key].initial_strike_time_ms = (initial_strike_time > 0) ? initial_strike_time : INITIAL_STRIKE_TIME_MS;

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
      // When the strike is over, the first phase (or the hold)
      if ((current_time - key->initial_strike_start_time) >= key->initial_strike_time_ms)
      {
        StartPhase(key, i, 0, current_time);
      }
      break;

    case KEY_STATE_PHASE:
      // When this phase is over, the next one (or the hold)
      if ((current_time - key->phase_start_time) >= key->phases[key->phase].time_ms)
      {
        StartPhase(key, i, key->phase + 1, current_time);
      }
      break;

    case KEY_STATE_HOLD:
      // Continue holding at hold duty cycle
      // This state is maintained until key is released
      break;
    }
  }
}
