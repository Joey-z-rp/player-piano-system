#include "stm32f1xx_hal.h"
#include "gpio_config.h"
#include "pwm_output_config.h"
#include "key_driver.h"
#include "rs485.h"
#include "command_parser.h"

static uint32_t last_update_time = 0;
static const uint32_t UPDATE_INTERVAL_MS = 1;

int main(void)
{
  HAL_Init();

  GPIO_Init();
  PWM_Init();
  PWM_Start();
  RS485_Init();
  KeyDriver_Init(&g_key_driver);
  CommandParser_Init(&g_key_driver);

  last_update_time = HAL_GetTick();

  // Main loop - non-blocking
  while (1)
  {
    uint32_t current_time = HAL_GetTick();

    if ((current_time - last_update_time) >= UPDATE_INTERVAL_MS)
    {
      KeyDriver_Update(&g_key_driver);

      CommandParser_ProcessQueue(CommandParser_GetQueue(), &g_key_driver);

      last_update_time = current_time;
    }
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
