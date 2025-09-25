#include "system_config.h"

// Timer handles (externally defined in main.c)
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;

/**
 * @brief Initialize a timer for PWM with given configuration
 * @param htim Timer handle
 * @param config Timer configuration
 * @return SYSTEM_OK on success, error code on failure
 */
system_status_t System_InitTimer(TIM_HandleTypeDef *htim, const timer_config_t *config)
{
  TIM_OC_InitTypeDef sConfigOC = {0};

  // Configure timer base
  htim->Instance = config->Instance;
  htim->Init.Prescaler = config->Prescaler;
  htim->Init.CounterMode = config->CounterMode;
  htim->Init.Period = config->Period;
  htim->Init.ClockDivision = config->ClockDivision;
  htim->Init.AutoReloadPreload = config->AutoReloadPreload;

  HAL_TIM_Base_Init(htim);

  HAL_TIM_PWM_Init(htim);

  // Configure output compare for PWM
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

  // Configure all 4 channels
  HAL_TIM_PWM_ConfigChannel(htim, &sConfigOC, TIM_CHANNEL_1);
  HAL_TIM_PWM_ConfigChannel(htim, &sConfigOC, TIM_CHANNEL_2);
  HAL_TIM_PWM_ConfigChannel(htim, &sConfigOC, TIM_CHANNEL_3);
  HAL_TIM_PWM_ConfigChannel(htim, &sConfigOC, TIM_CHANNEL_4);

  Enable_Timer_CLK(htim);
  return SYSTEM_OK;
}

/**
 * @brief Initialize Timer 1 for PWM
 * @return SYSTEM_OK on success, error code on failure
 */
static system_status_t System_InitTimer1(void)
{
  timer_config_t config = {
      .Instance = TIM1,
      .Period = 3599, // For 20kHz at 72MHz: (72MHz / 20000) - 1 = 3599
      .Prescaler = 0,
      .ClockDivision = TIM_CLOCKDIVISION_DIV1,
      .CounterMode = TIM_COUNTERMODE_UP,
      .AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE};

  return System_InitTimer(&htim1, &config);
}

/**
 * @brief Initialize Timer 2 for PWM
 * @return SYSTEM_OK on success, error code on failure
 */
static system_status_t System_InitTimer2(void)
{
  timer_config_t config = {
      .Instance = TIM2,
      .Period = 3599, // For 20kHz at 72MHz: (72MHz / 20000) - 1 = 3599
      .Prescaler = 0,
      .ClockDivision = TIM_CLOCKDIVISION_DIV1,
      .CounterMode = TIM_COUNTERMODE_UP,
      .AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE};

  return System_InitTimer(&htim2, &config);
}

/**
 * @brief Initialize Timer 3 for PWM
 * @return SYSTEM_OK on success, error code on failure
 */
static system_status_t System_InitTimer3(void)
{
  timer_config_t config = {
      .Instance = TIM3,
      .Period = 3599, // For 20kHz at 72MHz: (72MHz / 20000) - 1 = 3599
      .Prescaler = 0,
      .ClockDivision = TIM_CLOCKDIVISION_DIV1,
      .CounterMode = TIM_COUNTERMODE_UP,
      .AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE};

  return System_InitTimer(&htim3, &config);
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
void Enable_Timer_CLK(TIM_HandleTypeDef *htim)
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
