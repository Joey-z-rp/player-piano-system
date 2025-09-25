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

/**
 * @brief Initialize system clock configuration
 * @return SYSTEM_OK on success, error code on failure
 */
system_status_t System_InitClock(void);

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
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

#endif // SYSTEM_CONFIG_H
