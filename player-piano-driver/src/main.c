#include "pwm_output.h"
#include "system_config.h"

// Timer handles (required by PWM module)
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

int main(void)
{
  // Initialize HAL
  HAL_Init();

  // Initialize system (clock, GPIO, timers)
  if (System_Init() != SYSTEM_OK)
  {
    // System initialization failed
    while (1)
      ;
  }

  // Initialize PWM module
  if (PWM_Init() != PWM_OK)
  {
    // PWM initialization failed
    while (1)
      ;
  }

  // Main application loop
  while (1)
  {
    // Example: Set different duty cycles for demonstration
    PWM_SetDutyCycle(PWM_CHANNEL_0, 25);  // 25% duty cycle
    PWM_SetDutyCycle(PWM_CHANNEL_1, 50);  // 50% duty cycle
    PWM_SetDutyCycle(PWM_CHANNEL_2, 75);  // 75% duty cycle
    PWM_SetDutyCycle(PWM_CHANNEL_3, 100); // 100% duty cycle

    // Your player piano control logic goes here
    // For example:
    // - Read MIDI input
    // - Process note events
    // - Update PWM duty cycles based on note velocity
    // - Handle timing and sequencing

    HAL_Delay(100); // Small delay for demonstration
  }
}
