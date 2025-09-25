#include "pwm_output.h"
#include "system_config.h"

// Timer handles (required by PWM module)
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

int main(void)
{
  // Initialize HAL
  HAL_Init();

  System_Init();
  PWM_Init();

  uint8_t duty_cycle = 0;

  // Main application loop
  while (1)
  {
    if (duty_cycle == 0)
      duty_cycle = 70;
    else
      duty_cycle = 0;

    PWM_SetDutyCycle(PWM_CHANNEL_0, duty_cycle);
    PWM_SetDutyCycle(PWM_CHANNEL_1, duty_cycle);
    PWM_SetDutyCycle(PWM_CHANNEL_2, duty_cycle);
    PWM_SetDutyCycle(PWM_CHANNEL_3, duty_cycle);
    PWM_SetDutyCycle(PWM_CHANNEL_4, duty_cycle);
    PWM_SetDutyCycle(PWM_CHANNEL_5, duty_cycle);
    PWM_SetDutyCycle(PWM_CHANNEL_6, duty_cycle);
    PWM_SetDutyCycle(PWM_CHANNEL_7, duty_cycle);
    PWM_SetDutyCycle(PWM_CHANNEL_8, duty_cycle);
    PWM_SetDutyCycle(PWM_CHANNEL_9, duty_cycle);
    PWM_SetDutyCycle(PWM_CHANNEL_10, duty_cycle);
    PWM_SetDutyCycle(PWM_CHANNEL_11, duty_cycle);

    HAL_Delay(1000);
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
