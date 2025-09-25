#include "system_config.h"

// Timer handles (externally defined in main.c)
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;

/**
 * @brief Initialize system clock configuration
 * @return SYSTEM_OK on success, error code on failure
 */
system_status_t System_InitClock(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    return SYSTEM_ERROR_CLOCK_CONFIG;
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    return SYSTEM_ERROR_CLOCK_CONFIG;
  }

  return SYSTEM_OK;
}

/**
 * @brief Initialize Timer 1 for PWM
 * @return SYSTEM_OK on success, error code on failure
 */
static system_status_t System_InitTimer1(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 3599; // For 20kHz at 72MHz: (72MHz / 20000) - 1 = 3599
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;

  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  HAL_TIM_MspPostInit(&htim1);
  return SYSTEM_OK;
}

/**
 * @brief Initialize Timer 2 for PWM
 * @return SYSTEM_OK on success, error code on failure
 */
static system_status_t System_InitTimer2(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 3599; // For 20kHz at 72MHz: (72MHz / 20000) - 1 = 3599
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  HAL_TIM_MspPostInit(&htim2);
  return SYSTEM_OK;
}

/**
 * @brief Initialize Timer 3 for PWM
 * @return SYSTEM_OK on success, error code on failure
 */
static system_status_t System_InitTimer3(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 3599; // For 20kHz at 72MHz: (72MHz / 20000) - 1 = 3599
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    return SYSTEM_ERROR_TIMER_CONFIG;
  }

  HAL_TIM_MspPostInit(&htim3);
  return SYSTEM_OK;
}

/**
 * @brief Initialize all system timers for PWM
 * @return SYSTEM_OK on success, error code on failure
 */
system_status_t System_InitTimers(void)
{
  system_status_t status;

  status = System_InitTimer1();
  if (status != SYSTEM_OK)
    return status;

  status = System_InitTimer2();
  if (status != SYSTEM_OK)
    return status;

  status = System_InitTimer3();
  if (status != SYSTEM_OK)
    return status;

  return SYSTEM_OK;
}

/**
 * @brief Initialize all system GPIO
 * @return SYSTEM_OK on success, error code on failure
 */
system_status_t System_InitGPIO(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // Enable GPIO clocks for all ports used
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  // Configure Timer 1 PWM pins (PA8, PA9, PA10, PA11)
  GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // Configure Timer 2 PWM pins (PA0, PA1, PA2, PA3)
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // Configure Timer 3 PWM pins (PA6, PA7, PB0, PB1)
  GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  return SYSTEM_OK;
}

/**
 * @brief Initialize the entire system (clock, timers, GPIO)
 * @return SYSTEM_OK on success, error code on failure
 */
system_status_t System_Init(void)
{
  system_status_t status;

  // Initialize system clock
  status = System_InitClock();
  if (status != SYSTEM_OK)
    return status;

  // Initialize GPIO
  status = System_InitGPIO();
  if (status != SYSTEM_OK)
    return status;

  // Initialize timers
  status = System_InitTimers();
  if (status != SYSTEM_OK)
    return status;

  return SYSTEM_OK;
}

/**
 * @brief TIM MSP Initialization
 */
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM1)
  {
    __HAL_RCC_TIM1_CLK_ENABLE();
  }
  else if (htim->Instance == TIM2)
  {
    __HAL_RCC_TIM2_CLK_ENABLE();
  }
  else if (htim->Instance == TIM3)
  {
    __HAL_RCC_TIM3_CLK_ENABLE();
  }
}
