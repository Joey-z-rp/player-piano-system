#include "stm32f1xx_hal.h"
#include "gpio_config.h"
#include "pwm_output_config.h"
#include "key_driver.h"
#include "rs485.h"
#include "command_parser.h"
#include "stepper_motor.h"

static uint32_t last_update_time = 0;
static const uint32_t UPDATE_INTERVAL_MS = 1;

// Stepper motor demo variables
static uint32_t demo_start_time = 0;
static uint8_t demo_phase = 0;

// Global stepper motor instance
extern StepperMotor_t g_stepper_motor;

int main(void)
{
  HAL_Init();

  GPIO_Init();
  PWM_Init();
  PWM_Start();
  RS485_Init();
  KeyDriver_Init(&g_key_driver);
  CommandParser_Init(&g_key_driver);

  // Initialize stepper motor
  StepperMotor_Init(&g_stepper_motor);
  StepperMotor_SetSpeed(&g_stepper_motor, 1000);

  last_update_time = HAL_GetTick();
  demo_start_time = HAL_GetTick();

  // Main loop - non-blocking
  while (1)
  {
    uint32_t current_time = HAL_GetTick();

    if ((current_time - last_update_time) >= UPDATE_INTERVAL_MS)
    {
      KeyDriver_Update(&g_key_driver);
      CommandParser_ProcessQueue(CommandParser_GetQueue(), &g_key_driver);

      // Update stepper motor
      StepperMotor_Update(&g_stepper_motor);

      if ((current_time - demo_start_time) >= 150)
      {
        if (!StepperMotor_IsMoving(&g_stepper_motor))
        {
          if (demo_phase == 0)
          {
            StepperMotor_MoveRelative(&g_stepper_motor, 100); // Move 100 steps forward
            demo_phase = 1;
          }
          else
          {
            StepperMotor_MoveRelative(&g_stepper_motor, -100); // Move 100 steps back
            demo_phase = 0;
          }

          demo_start_time = current_time;
        }
      }

      last_update_time = current_time;
    }
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
