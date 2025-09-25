#ifndef PWM_CONFIG_H
#define PWM_CONFIG_H

// Include necessary STM32 HAL headers
#include "stm32f1xx_hal.h"

// PWM Configuration Structure
typedef struct
{
  TIM_HandleTypeDef htim;          // Timer handle
  uint32_t channel;                // Timer channel
  uint32_t frequency;              // PWM frequency in Hz
  uint32_t resolution;             // PWM resolution (period)
  uint32_t pulse_width_percentage; // Pulse width percentage (0-100, where 0%=1ms, 100%=2ms)
} PWM_Config_t;

// PWM Channel definitions for PA0, PA1, PA2, PA3
#define PWM_PA0_CHANNEL TIM_CHANNEL_1
#define PWM_PA1_CHANNEL TIM_CHANNEL_2
#define PWM_PA2_CHANNEL TIM_CHANNEL_3
#define PWM_PA3_CHANNEL TIM_CHANNEL_4

// Function declarations
void PWM_Init(void);
void PWM_SetPulseWidthPercentage(uint32_t channel, uint32_t pulse_width_percentage);
void PWM_Start(void);
void PWM_Stop(void);
void PWM_SetFrequency(uint32_t frequency);

// External timer handles for main access
extern TIM_HandleTypeDef htim2;

#endif // PWM_CONFIG_H
