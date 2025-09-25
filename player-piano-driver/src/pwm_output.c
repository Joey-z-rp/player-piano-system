#include "pwm_output.h"

// External timer handles (to be defined in main.c)
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;

// PWM channel configuration structure
typedef struct
{
  TIM_HandleTypeDef *timer;
  uint32_t channel;
  GPIO_TypeDef *port;
  uint16_t pin;
  uint8_t duty_cycle;
} pwm_channel_config_t;

// PWM channel configurations
static pwm_channel_config_t pwm_channels[PWM_CHANNELS] = {
    // Timer 1 channels (4 channels)
    {&htim1, TIM_CHANNEL_1, GPIOA, GPIO_PIN_8, 0},  // Channel 0
    {&htim1, TIM_CHANNEL_2, GPIOA, GPIO_PIN_9, 0},  // Channel 1
    {&htim1, TIM_CHANNEL_3, GPIOA, GPIO_PIN_10, 0}, // Channel 2
    {&htim1, TIM_CHANNEL_4, GPIOA, GPIO_PIN_11, 0}, // Channel 3

    // Timer 2 channels (4 channels)
    {&htim2, TIM_CHANNEL_1, GPIOA, GPIO_PIN_0, 0}, // Channel 4
    {&htim2, TIM_CHANNEL_2, GPIOA, GPIO_PIN_1, 0}, // Channel 5
    {&htim2, TIM_CHANNEL_3, GPIOA, GPIO_PIN_2, 0}, // Channel 6
    {&htim2, TIM_CHANNEL_4, GPIOA, GPIO_PIN_3, 0}, // Channel 7

    // Timer 3 channels (4 channels)
    {&htim3, TIM_CHANNEL_1, GPIOA, GPIO_PIN_6, 0}, // Channel 8
    {&htim3, TIM_CHANNEL_2, GPIOA, GPIO_PIN_7, 0}, // Channel 9
    {&htim3, TIM_CHANNEL_3, GPIOB, GPIO_PIN_0, 0}, // Channel 10
    {&htim3, TIM_CHANNEL_4, GPIOB, GPIO_PIN_1, 0}  // Channel 11
};

/**
 * @brief Initialize the PWM output module
 * @return PWM_OK on success, error code on failure
 */
pwm_status_t PWM_Init(void)
{
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);

  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);

  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

  // Initialize all channels to 0% duty cycle
  for (int i = 0; i < PWM_CHANNELS; i++)
  {
    pwm_channels[i].duty_cycle = 0;

    // Set initial duty cycle to 0
    __HAL_TIM_SET_COMPARE(pwm_channels[i].timer, pwm_channels[i].channel, 0);
  }

  return PWM_OK;
}

/**
 * @brief Set the duty cycle for a specific PWM channel
 * @param channel PWM channel (0-11)
 * @param duty_cycle Duty cycle percentage (0-100)
 * @return PWM_OK on success, error code on failure
 */
pwm_status_t PWM_SetDutyCycle(pwm_channel_t channel, uint8_t duty_cycle)
{
  // Validate inputs
  if (channel >= PWM_CHANNELS)
  {
    return PWM_ERROR_INVALID_CHANNEL;
  }

  if (duty_cycle > PWM_MAX_DUTY_CYCLE)
  {
    return PWM_ERROR_INVALID_DUTY_CYCLE;
  }

  // Calculate the compare value based on duty cycle
  uint32_t period = pwm_channels[channel].timer->Init.Period;
  uint32_t compare_value = (duty_cycle * period) / PWM_MAX_DUTY_CYCLE;

  // Set the compare value
  __HAL_TIM_SET_COMPARE(pwm_channels[channel].timer, pwm_channels[channel].channel, compare_value);

  // Update the stored duty cycle
  pwm_channels[channel].duty_cycle = duty_cycle;

  return PWM_OK;
}
