#include "stm32f1xx_hal.h"

int main(void)
{
  HAL_Init();

  while (1)
  {
    // Send message via RS-485 (MAX485)
    HAL_Delay(1);
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
