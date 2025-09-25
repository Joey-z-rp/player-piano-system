#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include "stm32f1xx_hal.h"

// System Status enumeration
typedef enum
{
  SYSTEM_OK = 0,
  SYSTEM_ERROR_CLOCK_CONFIG,
  SYSTEM_ERROR_TIMER_CONFIG,
  SYSTEM_ERROR_GPIO_CONFIG
} system_status_t;

// Timer configuration structure
typedef struct
{
  TIM_TypeDef *Instance;
  uint32_t Period;
  uint32_t Prescaler;
  uint32_t ClockDivision;
  uint32_t CounterMode;
  uint32_t AutoReloadPreload;
} timer_config_t;

/**
 * @brief Initialize a timer for PWM with given configuration
 * @param htim Timer handle
 * @param config Timer configuration
 * @return SYSTEM_OK on success, error code on failure
 */
system_status_t System_InitTimer(TIM_HandleTypeDef *htim, const timer_config_t *config);

/**
 * @brief Initialize all system timers for PWM
 * @return SYSTEM_OK on success, error code on failure
 */
system_status_t System_InitTimers(void);

/**
 * @brief Initialize all system GPIO
 * @return SYSTEM_OK on success, error code on failure
 */
system_status_t System_InitGPIO(void);

/**
 * @brief Initialize the entire system (clock, timers, GPIO)
 * @return SYSTEM_OK on success, error code on failure
 */
system_status_t System_Init(void);

/**
 * @brief TIM MSP Post Initialization function
 * @param htim TIM handle
 */
void Enable_Timer_CLK(TIM_HandleTypeDef *htim);

#endif // SYSTEM_CONFIG_H
