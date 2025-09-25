#ifndef GPIO_CONFIG_H
#define GPIO_CONFIG_H

// Include necessary STM32 HAL headers
#include "stm32f1xx_hal.h"

// GPIO Pin Configuration Structure
typedef struct
{
  GPIO_TypeDef *port; // GPIO port (GPIOA, GPIOB, GPIOC, etc.)
  uint16_t pin;       // GPIO pin number
  uint32_t mode;      // GPIO mode (GPIO_MODE_OUTPUT_PP, GPIO_MODE_INPUT, etc.)
  uint32_t pull;      // GPIO pull-up/down configuration
  uint32_t speed;     // GPIO speed configuration
} GPIO_PinConfig_t;

void GPIO_Init(void);

void GPIO_EnablePortClock(GPIO_TypeDef *port);

void GPIO_InitPin(const GPIO_PinConfig_t *config);

void GPIO_InitMultiplePins(const GPIO_PinConfig_t *configs, uint8_t count);

#endif // GPIO_CONFIG_H
