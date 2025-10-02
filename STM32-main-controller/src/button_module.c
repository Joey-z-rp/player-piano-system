#include "button_module.h"
#include <stdio.h>

// Global button module instance
static ButtonModule_t g_button_module;

/**
 * @brief Initialize button module
 * @param button: Pointer to button module structure
 */
void ButtonModule_Init(ButtonModule_t *button)
{
  if (button == NULL)
  {
    return;
  }

  // Configure button pin
  button->port = BUTTON_PIN_PORT;
  button->pin = BUTTON_PIN_NUMBER;
  button->current_state = BUTTON_STATE_RELEASED;
  button->last_change_time = 0;
  button->debounce_active = 0;

  // Enable GPIO clock
  __HAL_RCC_GPIOC_CLK_ENABLE();

  // Configure GPIO pin for input with pull-up
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = button->pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(button->port, &GPIO_InitStruct);
}

/**
 * @brief Update button state with debouncing
 * @param button: Pointer to button module structure
 */
void ButtonModule_Update(ButtonModule_t *button)
{
  if (button == NULL)
  {
    return;
  }

  uint32_t current_time = HAL_GetTick();

  // Read current pin state (inverted because we're using pull-up)
  GPIO_PinState pin_state = HAL_GPIO_ReadPin(button->port, button->pin);
  ButtonState_t new_state = (pin_state == GPIO_PIN_RESET) ? BUTTON_STATE_PRESSED : BUTTON_STATE_RELEASED;

  // Check if state has changed
  if (new_state != button->current_state)
  {
    // Start debounce timer
    if (!button->debounce_active)
    {
      button->debounce_active = 1;
      button->last_change_time = current_time;
    }
    else
    {
      // Check if debounce time has elapsed
      if ((current_time - button->last_change_time) >= BUTTON_DEBOUNCE_TIME_MS)
      {
        // Update state
        button->current_state = new_state;
        button->debounce_active = 0;

        // Send appropriate command based on state change
        if (button->current_state == BUTTON_STATE_PRESSED)
        {
          // Max duty cycle 80
          // Button pressed - send "P:0:100\n"
          RS485_SendString("P:11:80\n");
        }
        else
        {
          // Button released - send "R:0:0\n"
          RS485_SendString("R:11:0\n");
        }
      }
    }
  }
  else
  {
    // No state change, reset debounce
    button->debounce_active = 0;
  }
}

/**
 * @brief Get global button module instance
 * @return Pointer to global button module
 */
ButtonModule_t *ButtonModule_GetInstance(void)
{
  return &g_button_module;
}
