#include "stm32f1xx_hal.h"
#include "gpio_config.h"
#include "pwm_output_config.h"
#include "key_driver.h"
#include "rs485.h"
#include <string.h>
#include <stdio.h>

// RS485 message callback function
void RS485_MessageReceived(const char *message, uint16_t length)
{
  volatile uint8_t message_length = length;
  // Process received message
  // Example: Parse commands and control piano keys
  if (strncmp(message, "KEY:", 4) == 0)
  {
    // Parse key command: "KEY:1:64" (key number:velocity)
    int key_num, velocity;
    if (sscanf(message + 4, "%d:%d", &key_num, &velocity) == 2)
    {
      if (key_num >= 0 && key_num < NUM_KEYS)
      {
        KeyDriver_PressKey(&g_key_driver, key_num, velocity);
      }
    }
  }
  else if (strncmp(message, "RELEASE:", 8) == 0)
  {
    // Parse release command: "RELEASE:1"
    int key_num;
    if (sscanf(message + 8, "%d", &key_num) == 1)
    {
      if (key_num >= 0 && key_num < NUM_KEYS)
      {
        KeyDriver_ReleaseKey(&g_key_driver, key_num);
      }
    }
  }
}

int main(void)
{
  HAL_Init();

  GPIO_Init();
  PWM_Init();
  PWM_Start();

  // Initialize key driver module
  KeyDriver_Init(&g_key_driver);

  // Initialize RS485 module
  RS485_Init();
  RS485_SetMessageCallback(RS485_MessageReceived);

  // Example usage: Press and release keys with different velocities
  uint32_t last_key_press = 0;
  uint8_t current_key = 0;
  uint8_t current_velocity = 64; // Medium velocity

  while (1)
  {
    // Update key driver state machine
    KeyDriver_Update(&g_key_driver);

    // // Example: Cycle through keys every 2 seconds
    // if (HAL_GetTick() - last_key_press > 2000)
    // {
    //   // Release previous key
    //   if (current_key > 0)
    //   {
    //     KeyDriver_ReleaseKey(&g_key_driver, current_key - 1);
    //   }

    //   // Press current key with varying velocity
    //   KeyDriver_PressKey(&g_key_driver, current_key, current_velocity);

    //   // Update for next iteration
    //   current_key = (current_key + 1) % NUM_KEYS;
    //   current_velocity = (current_velocity + 20) % 128; // Vary velocity
    //   last_key_press = HAL_GetTick();
    // }

    // Small delay to prevent excessive CPU usage
    HAL_Delay(1);
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
