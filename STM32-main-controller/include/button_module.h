#ifndef BUTTON_MODULE_H
#define BUTTON_MODULE_H

#include "stm32f1xx_hal.h"
#include "rs485.h"

// Button configuration
#define BUTTON_DEBOUNCE_TIME_MS 20
#define BUTTON_PIN_PORT GPIOC
#define BUTTON_PIN_NUMBER GPIO_PIN_14

// Button states
typedef enum
{
  BUTTON_STATE_RELEASED = 0,
  BUTTON_STATE_PRESSED
} ButtonState_t;

// Button module structure
typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  ButtonState_t current_state;
  uint32_t last_change_time;
  uint8_t debounce_active;
} ButtonModule_t;

// Function prototypes
void ButtonModule_Init(ButtonModule_t *button);
void ButtonModule_Update(ButtonModule_t *button);

// Global instance access
ButtonModule_t *ButtonModule_GetInstance(void);

#endif // BUTTON_MODULE_H
