#include "stm32f1xx_hal.h"
#include "rs485.h"
#include "button_module.h"
#include <stdio.h>

int main(void)
{
  HAL_Init();

  // Initialize RS485 module
  if (RS485_Init() != HAL_OK)
  {
    // Error handling
    while (1)
      ;
  }

  // Initialize button module
  ButtonModule_Init(ButtonModule_GetInstance());

  while (1)
  {
    // Update button module to check for button presses
    ButtonModule_Update(ButtonModule_GetInstance());

    // Small delay to prevent excessive CPU usage
    HAL_Delay(1);
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
