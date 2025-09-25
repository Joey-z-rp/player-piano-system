#include "stm32f1xx_hal.h"
#include "gpio_config.h"
#include "pwm_output_config.h"

int main(void)
{
  HAL_Init();

  GPIO_Init();
  PWM_Init();

  PWM_Start();

  uint8_t hold_duty_cycle = 20;
  uint8_t initial_duty_cycle = 60;

  while (1)
  {
    PWM_SetDutyCycle(0, initial_duty_cycle);
    HAL_Delay(50);
    PWM_SetDutyCycle(0, hold_duty_cycle);
    HAL_Delay(1000);
    PWM_SetDutyCycle(0, 0);

    // PWM_SetDutyCycle(1, duty_cycle);
    // PWM_SetDutyCycle(2, duty_cycle);
    // PWM_SetDutyCycle(3, duty_cycle);
    // PWM_SetDutyCycle(4, duty_cycle);
    // PWM_SetDutyCycle(5, duty_cycle);
    // PWM_SetDutyCycle(6, duty_cycle);
    // PWM_SetDutyCycle(7, duty_cycle);
    // PWM_SetDutyCycle(8, duty_cycle);
    // PWM_SetDutyCycle(9, duty_cycle);
    // PWM_SetDutyCycle(10, duty_cycle);
    // PWM_SetDutyCycle(11, duty_cycle);

    HAL_Delay(1000);
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
