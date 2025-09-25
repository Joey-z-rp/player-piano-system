#ifndef PWM_OUTPUT_H
#define PWM_OUTPUT_H

#include "stm32f1xx_hal.h"

// PWM Configuration
#define PWM_FREQUENCY_HZ 20000 // 20kHz
#define PWM_CHANNELS 12        // Number of PWM channels
#define PWM_MAX_DUTY_CYCLE 100 // Maximum duty cycle percentage

// PWM Channel enumeration
typedef enum
{
  PWM_CHANNEL_0 = 0,
  PWM_CHANNEL_1,
  PWM_CHANNEL_2,
  PWM_CHANNEL_3,
  PWM_CHANNEL_4,
  PWM_CHANNEL_5,
  PWM_CHANNEL_6,
  PWM_CHANNEL_7,
  PWM_CHANNEL_8,
  PWM_CHANNEL_9,
  PWM_CHANNEL_10,
  PWM_CHANNEL_11
} pwm_channel_t;

// PWM Status enumeration
typedef enum
{
  PWM_OK = 0,
  PWM_ERROR_INVALID_CHANNEL,
  PWM_ERROR_INVALID_DUTY_CYCLE,
  PWM_ERROR_NOT_INITIALIZED
} pwm_status_t;

/**
 * @brief Initialize the PWM output module
 * @return PWM_OK on success, error code on failure
 */
pwm_status_t PWM_Init(void);

/**
 * @brief Set the duty cycle for a specific PWM channel
 * @param channel PWM channel (0-11)
 * @param duty_cycle Duty cycle percentage (0-100)
 * @return PWM_OK on success, error code on failure
 */
pwm_status_t PWM_SetDutyCycle(pwm_channel_t channel, uint8_t duty_cycle);

#endif // PWM_OUTPUT_H
