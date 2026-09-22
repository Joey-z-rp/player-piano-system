#ifndef PWM_CONFIG_H
#define PWM_CONFIG_H

// Include necessary STM32 HAL headers
#include "stm32f1xx_hal.h"

// PWM Configuration Structure
typedef struct
{
  TIM_HandleTypeDef *htim; // Pointer to timer handle
  uint32_t channel;        // Timer channel
  uint32_t frequency;      // PWM frequency in Hz
  uint32_t resolution;     // PWM resolution (period)
  uint32_t duty_cycle;     // Duty cycle percentage (0-100)
} PWM_Config_t;

#define PWM_NUM_CHANNELS 15

// PWM Channel definitions for 15 outputs
// TIM2 channels (PA0, PA1, PA2, PA3)
#define PWM_PA0_CHANNEL TIM_CHANNEL_1
#define PWM_PA1_CHANNEL TIM_CHANNEL_2
#define PWM_PA2_CHANNEL TIM_CHANNEL_3
#define PWM_PA3_CHANNEL TIM_CHANNEL_4

// TIM3 channels (PA6, PA7, PB0, PB1)
#define PWM_PA6_CHANNEL TIM_CHANNEL_1
#define PWM_PA7_CHANNEL TIM_CHANNEL_2
#define PWM_PB0_CHANNEL TIM_CHANNEL_3
#define PWM_PB1_CHANNEL TIM_CHANNEL_4

// TIM4 channels (PB6, PB7, PB8, PB9)
#define PWM_PB6_CHANNEL TIM_CHANNEL_1
#define PWM_PB7_CHANNEL TIM_CHANNEL_2
#define PWM_PB8_CHANNEL TIM_CHANNEL_3
#define PWM_PB9_CHANNEL TIM_CHANNEL_4

// TIM1 channels (PA8, PA9, PA10). PA11 left unused (USB DM)
#define PWM_PA8_CHANNEL TIM_CHANNEL_1
#define PWM_PA9_CHANNEL TIM_CHANNEL_2
#define PWM_PA10_CHANNEL TIM_CHANNEL_3

// Function declarations
void PWM_Init(void);
void PWM_SetDutyCycle(uint8_t channel_index, uint32_t duty_cycle); // 0=PB1, 1=PB0, 2=PA7, 3=PA6, 4=PA3, 5=PA2, 6=PA1, 7=PA0, 8=PB9, 9=PB8, 10=PB7, 11=PB6, 12=PA10, 13=PA9, 14=PA8
void PWM_Start(void);
void PWM_Stop(void);

// External timer handles for main access
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;

#endif // PWM_CONFIG_H
