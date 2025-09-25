#include "pwm_output_config.h"

// Timer handles for PWM generation
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

// PWM configuration for all 12 outputs at 20kHz
static const PWM_Config_t pwm_configs[] = {
    // TIM2 channels (PA0, PA1, PA2, PA3)
    {.htim = &htim2, .channel = PWM_PA0_CHANNEL, .frequency = 20000, .resolution = 100, .duty_cycle = 50},
    {.htim = &htim2, .channel = PWM_PA1_CHANNEL, .frequency = 20000, .resolution = 100, .duty_cycle = 50},
    {.htim = &htim2, .channel = PWM_PA2_CHANNEL, .frequency = 20000, .resolution = 100, .duty_cycle = 50},
    {.htim = &htim2, .channel = PWM_PA3_CHANNEL, .frequency = 20000, .resolution = 100, .duty_cycle = 50},

    // TIM3 channels (PA6, PA7, PB0, PB1)
    {.htim = &htim3, .channel = PWM_PA6_CHANNEL, .frequency = 20000, .resolution = 100, .duty_cycle = 50},
    {.htim = &htim3, .channel = PWM_PA7_CHANNEL, .frequency = 20000, .resolution = 100, .duty_cycle = 50},
    {.htim = &htim3, .channel = PWM_PB0_CHANNEL, .frequency = 20000, .resolution = 100, .duty_cycle = 50},
    {.htim = &htim3, .channel = PWM_PB1_CHANNEL, .frequency = 20000, .resolution = 100, .duty_cycle = 50},

    // TIM4 channels (PB6, PB7, PB8, PB9)
    {.htim = &htim4, .channel = PWM_PB6_CHANNEL, .frequency = 20000, .resolution = 100, .duty_cycle = 50},
    {.htim = &htim4, .channel = PWM_PB7_CHANNEL, .frequency = 20000, .resolution = 100, .duty_cycle = 50},
    {.htim = &htim4, .channel = PWM_PB8_CHANNEL, .frequency = 20000, .resolution = 100, .duty_cycle = 50},
    {.htim = &htim4, .channel = PWM_PB9_CHANNEL, .frequency = 20000, .resolution = 100, .duty_cycle = 50}};

// Helper function to configure a timer for PWM
static void PWM_ConfigureTimer(TIM_HandleTypeDef *htim, uint32_t frequency, uint32_t resolution)
{
  htim->Init.Prescaler = (SystemCoreClock / (frequency * resolution)) - 1;
  htim->Init.CounterMode = TIM_COUNTERMODE_UP;
  htim->Init.Period = resolution - 1;
  htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
}

// Helper function to configure PWM channels for a timer
static void PWM_ConfigureChannels(TIM_HandleTypeDef *htim, const PWM_Config_t *configs, uint8_t start_idx, uint8_t count)
{
  TIM_OC_InitTypeDef sConfigOC = {0};
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;

  for (uint8_t i = 0; i < count; i++)
  {
    const PWM_Config_t *config = &configs[start_idx + i];
    sConfigOC.Pulse = (config->duty_cycle * config->resolution) / 100;

    if (HAL_TIM_PWM_ConfigChannel(htim, &sConfigOC, config->channel) != HAL_OK)
    {
      // Error handling - could add error callback here
    }
  }
}

void PWM_Init(void)
{
  // Enable timer clocks
  __HAL_RCC_TIM2_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();
  __HAL_RCC_TIM4_CLK_ENABLE();

  // Enable GPIO clocks
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // Configure GPIO pins for PWM output
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // Configure PA0, PA1, PA2, PA3, PA6, PA7 for PWM output
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_6 | GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // Configure PB0, PB1, PB6, PB7, PB8, PB9 for PWM output
  GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // Configure TIM2
  htim2.Instance = TIM2;
  PWM_ConfigureTimer(&htim2, pwm_configs[0].frequency, pwm_configs[0].resolution);
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    // Error handling
  }
  PWM_ConfigureChannels(&htim2, pwm_configs, 0, 4);

  // Configure TIM3
  htim3.Instance = TIM3;
  PWM_ConfigureTimer(&htim3, pwm_configs[4].frequency, pwm_configs[4].resolution);
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    // Error handling
  }
  PWM_ConfigureChannels(&htim3, pwm_configs, 4, 4);

  // Configure TIM4
  htim4.Instance = TIM4;
  PWM_ConfigureTimer(&htim4, pwm_configs[8].frequency, pwm_configs[8].resolution);
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    // Error handling
  }
  PWM_ConfigureChannels(&htim4, pwm_configs, 8, 4);
}

void PWM_SetDutyCycle(uint32_t channel, uint32_t duty_cycle)
{
  if (duty_cycle > 100)
    duty_cycle = 100;

  // Find the configuration for this channel
  for (uint8_t i = 0; i < 12; i++)
  {
    if (pwm_configs[i].channel == channel)
    {
      uint32_t pulse = (duty_cycle * pwm_configs[i].resolution) / 100;
      __HAL_TIM_SET_COMPARE(pwm_configs[i].htim, channel, pulse);
      break;
    }
  }
}

void PWM_Start(void)
{
  // Start all TIM2 channels
  HAL_TIM_PWM_Start(&htim2, PWM_PA0_CHANNEL);
  HAL_TIM_PWM_Start(&htim2, PWM_PA1_CHANNEL);
  HAL_TIM_PWM_Start(&htim2, PWM_PA2_CHANNEL);
  HAL_TIM_PWM_Start(&htim2, PWM_PA3_CHANNEL);

  // Start all TIM3 channels
  HAL_TIM_PWM_Start(&htim3, PWM_PA6_CHANNEL);
  HAL_TIM_PWM_Start(&htim3, PWM_PA7_CHANNEL);
  HAL_TIM_PWM_Start(&htim3, PWM_PB0_CHANNEL);
  HAL_TIM_PWM_Start(&htim3, PWM_PB1_CHANNEL);

  // Start all TIM4 channels
  HAL_TIM_PWM_Start(&htim4, PWM_PB6_CHANNEL);
  HAL_TIM_PWM_Start(&htim4, PWM_PB7_CHANNEL);
  HAL_TIM_PWM_Start(&htim4, PWM_PB8_CHANNEL);
  HAL_TIM_PWM_Start(&htim4, PWM_PB9_CHANNEL);
}

void PWM_Stop(void)
{
  // Stop all TIM2 channels
  HAL_TIM_PWM_Stop(&htim2, PWM_PA0_CHANNEL);
  HAL_TIM_PWM_Stop(&htim2, PWM_PA1_CHANNEL);
  HAL_TIM_PWM_Stop(&htim2, PWM_PA2_CHANNEL);
  HAL_TIM_PWM_Stop(&htim2, PWM_PA3_CHANNEL);

  // Stop all TIM3 channels
  HAL_TIM_PWM_Stop(&htim3, PWM_PA6_CHANNEL);
  HAL_TIM_PWM_Stop(&htim3, PWM_PA7_CHANNEL);
  HAL_TIM_PWM_Stop(&htim3, PWM_PB0_CHANNEL);
  HAL_TIM_PWM_Stop(&htim3, PWM_PB1_CHANNEL);

  // Stop all TIM4 channels
  HAL_TIM_PWM_Stop(&htim4, PWM_PB6_CHANNEL);
  HAL_TIM_PWM_Stop(&htim4, PWM_PB7_CHANNEL);
  HAL_TIM_PWM_Stop(&htim4, PWM_PB8_CHANNEL);
  HAL_TIM_PWM_Stop(&htim4, PWM_PB9_CHANNEL);
}