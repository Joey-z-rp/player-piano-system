#include "stm32f1xx_hal.h"
#include "gpio_config.h"
#include "pwm_output_config.h"
#include "key_driver.h"

int main(void)
{
  HAL_Init();

  GPIO_Init();
  PWM_Init();
  PWM_Start();

  // Initialize key driver module
  KeyDriver_Init(&g_key_driver);

  // Example usage: Press and release keys with different velocities
  uint32_t last_key_press = 0;
  uint8_t current_key = 0;
  uint8_t current_velocity = 64; // Medium velocity

  while (1)
  {
    // Update key driver state machine
    KeyDriver_Update(&g_key_driver);

    // Example: Cycle through keys every 2 seconds
    if (HAL_GetTick() - last_key_press > 2000)
    {
      // Release previous key
      if (current_key > 0)
      {
        KeyDriver_ReleaseKey(&g_key_driver, current_key - 1);
      }

      // Press current key with varying velocity
      KeyDriver_PressKey(&g_key_driver, current_key, current_velocity);

      // Update for next iteration
      current_key = (current_key + 1) % NUM_KEYS;
      current_velocity = (current_velocity + 20) % 128; // Vary velocity
      last_key_press = HAL_GetTick();
    }

    // Small delay to prevent excessive CPU usage
    HAL_Delay(1);
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
