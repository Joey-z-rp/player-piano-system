#include "pwm_output_config.h"

// Timer handle for PWM generation
TIM_HandleTypeDef htim2;

// PWM configuration for PA0, PA1, PA2, PA3
static const PWM_Config_t pwm_configs[] = {
    {.channel = PWM_PA0_CHANNEL, .frequency = 50, .resolution = 1000, .duty_cycle = 50},
    {.channel = PWM_PA1_CHANNEL, .frequency = 50, .resolution = 1000, .duty_cycle = 50},
    {.channel = PWM_PA2_CHANNEL, .frequency = 50, .resolution = 1000, .duty_cycle = 50},
    {.channel = PWM_PA3_CHANNEL, .frequency = 50, .resolution = 1000, .duty_cycle = 50}};

void PWM_Init(void)
{
  // Enable TIM2 clock
  __HAL_RCC_TIM2_CLK_ENABLE();

  // Enable GPIOA clock for PA0, PA1, PA2, PA3
  __HAL_RCC_GPIOA_CLK_ENABLE();

  // Configure PA0, PA1, PA2, PA3 as alternate function (PWM output)
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // Configure TIM2 for PWM generation
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = (SystemCoreClock / (pwm_configs[0].frequency * pwm_configs[0].resolution)) - 1;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = pwm_configs[0].resolution - 1;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    // Error handling - could add error callback here
  }

  // Configure PWM channels
  TIM_OC_InitTypeDef sConfigOC = {0};
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  // Convert duty cycle to compare value: duty_cycle% of resolution
  sConfigOC.Pulse = (pwm_configs[0].duty_cycle * pwm_configs[0].resolution) / 100;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

  // Configure all three channels
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, PWM_PA0_CHANNEL) != HAL_OK)
  {
    // Error handling
  }

  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, PWM_PA1_CHANNEL) != HAL_OK)
  {
    // Error handling
  }

  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, PWM_PA2_CHANNEL) != HAL_OK)
  {
    // Error handling
  }

  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, PWM_PA3_CHANNEL) != HAL_OK)
  {
    // Error handling
  }
}

void PWM_SetDutyCycle(uint32_t channel, uint32_t duty_cycle)
{
  if (duty_cycle > 100)
    duty_cycle = 100;

  // Convert duty cycle to compare value: duty_cycle% of resolution
  uint32_t pulse = (duty_cycle * pwm_configs[0].resolution) / 100;

  switch (channel)
  {
  case PWM_PA0_CHANNEL:
    __HAL_TIM_SET_COMPARE(&htim2, PWM_PA0_CHANNEL, pulse);
    break;
  case PWM_PA1_CHANNEL:
    __HAL_TIM_SET_COMPARE(&htim2, PWM_PA1_CHANNEL, pulse);
    break;
  case PWM_PA2_CHANNEL:
    __HAL_TIM_SET_COMPARE(&htim2, PWM_PA2_CHANNEL, pulse);
    break;
  case PWM_PA3_CHANNEL:
    __HAL_TIM_SET_COMPARE(&htim2, PWM_PA3_CHANNEL, pulse);
    break;
  default:
    break;
  }
}

void PWM_Start(void)
{
  HAL_TIM_PWM_Start(&htim2, PWM_PA0_CHANNEL);
  HAL_TIM_PWM_Start(&htim2, PWM_PA1_CHANNEL);
  HAL_TIM_PWM_Start(&htim2, PWM_PA2_CHANNEL);
  HAL_TIM_PWM_Start(&htim2, PWM_PA3_CHANNEL);
}

void PWM_Stop(void)
{
  HAL_TIM_PWM_Stop(&htim2, PWM_PA0_CHANNEL);
  HAL_TIM_PWM_Stop(&htim2, PWM_PA1_CHANNEL);
  HAL_TIM_PWM_Stop(&htim2, PWM_PA2_CHANNEL);
  HAL_TIM_PWM_Stop(&htim2, PWM_PA3_CHANNEL);
}
