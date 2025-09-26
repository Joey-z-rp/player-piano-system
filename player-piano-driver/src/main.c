#include "stm32f1xx_hal.h"
#include "gpio_config.h"
#include "pwm_output_config.h"
#include "key_driver.h"
#include "rs485.h"
#include "command_parser.h"

int main(void)
{
  HAL_Init();

  GPIO_Init();
  PWM_Init();
  PWM_Start();
  RS485_Init();
  KeyDriver_Init(&g_key_driver);
  CommandParser_Init(&g_key_driver);

  // Main loop
  while (1)
  {
    // Update key driver state machine
    KeyDriver_Update(&g_key_driver);

    // Small delay to prevent excessive CPU usage
    HAL_Delay(1);
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
