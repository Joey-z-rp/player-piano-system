#include "stm32f1xx_hal.h"
#include "gpio_config.h"
#include "pwm_output_config.h"

int main(void)
{
  HAL_Init();

  GPIO_Init();
  PWM_Init();

  PWM_Start();

  while (1)
  {
    HAL_Delay(1000);
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
