#ifndef PWM_CONFIG_H
#define PWM_CONFIG_H

// Include necessary STM32 HAL headers
#include "stm32f1xx_hal.h"

// PWM Configuration Structure
typedef struct
{
  TIM_HandleTypeDef htim; // Timer handle
  uint32_t channel;       // Timer channel
  uint32_t frequency;     // PWM frequency in Hz
  uint32_t resolution;    // PWM resolution (period)
  uint32_t duty_cycle;    // Duty cycle percentage (0-100)
} PWM_Config_t;

// PWM Channel definitions for PA0, PA1, PA2, PA3
#define PWM_PA0_CHANNEL TIM_CHANNEL_1
#define PWM_PA1_CHANNEL TIM_CHANNEL_2
#define PWM_PA2_CHANNEL TIM_CHANNEL_3
#define PWM_PA3_CHANNEL TIM_CHANNEL_4

// Function declarations
void PWM_Init(void);
void PWM_SetDutyCycle(uint32_t channel, uint32_t duty_cycle);
void PWM_Start(void);
void PWM_Stop(void);

// External timer handles for main access
extern TIM_HandleTypeDef htim2;

#endif // PWM_CONFIG_H
