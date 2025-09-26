#include "stm32f1xx_hal.h"
#include "rs485.h"

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

  // Example message to send
  char message[] = "@";

  while (1)
  {
    // Send string message via RS-485
    RS485_SendString(message);
    HAL_Delay(1000);
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
