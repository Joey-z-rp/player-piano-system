#include "stepper_motor.h"
#include "core_cm3.h" // For DWT registers

// Global stepper motor instance
StepperMotor_t g_stepper_motor;

// ADC handle for pressure sensor
static ADC_HandleTypeDef hadc1;

// Private variables for timing
static uint32_t step_delay_us = 1000; // Default delay
static uint32_t last_step_time = 0;

// State machine for step pulse generation
typedef enum
{
  STEP_IDLE,
  STEP_PULSE_HIGH,
  STEP_PULSE_LOW
} step_state_t;

static step_state_t step_state = STEP_IDLE;
static uint32_t step_pulse_start_time = 0;

// Get current time in microseconds using DWT
static uint32_t getMicroseconds(void)
{
  // Convert cycles to microseconds using SystemCoreClock
  return DWT->CYCCNT / (SystemCoreClock / 1000000);
}

// Calculate step delay based on speed
static uint32_t calculateStepDelay(uint32_t speed_steps_per_sec)
{
  if (speed_steps_per_sec == 0)
    return 1000000;                     // 1 second delay if speed is 0
  return 1000000 / speed_steps_per_sec; // Convert to microseconds
}

// Initialize ADC for pressure sensor
static void StepperMotor_InitADC(void)
{
  // Enable ADC1 clock
  __HAL_RCC_ADC1_CLK_ENABLE();

  // Configure ADC1
  hadc1.Instance = PRESSURE_SENSOR_ADC;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;

  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    // Error handling - could set an error flag here
  }

  // Configure ADC channel
  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Channel = PRESSURE_SENSOR_ADC_CHANNEL;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;

  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    // Error handling - could set an error flag here
  }
}

// Read pressure sensor value
uint16_t StepperMotor_ReadPressureSensor(void)
{
  uint16_t adc_value = 0;

  // Start ADC conversion
  if (HAL_ADC_Start(&hadc1) == HAL_OK)
  {
    // Wait for conversion to complete
    if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
    {
      adc_value = HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
  }

  return adc_value;
}

// Calibrate stepper motor position using pressure sensor
static void StepperMotor_CalibratePosition(StepperMotor_t *motor)
{
  // Set motor to slow speed for calibration
  uint32_t original_speed = motor->current_speed;
  StepperMotor_SetSpeed(motor, 200); // Slow speed for precise calibration

  // Set direction to backward (CCW)
  StepperMotor_SetDirection(motor, STEPPER_DIR_CCW);

  // Move backward in small steps until pressure sensor is pressed
  uint32_t step_count = 0;
  const uint32_t max_calibration_steps = 2000; // Safety limit to prevent infinite movement

  while (step_count < max_calibration_steps)
  {
    // Read pressure sensor
    uint16_t pressure_value = StepperMotor_ReadPressureSensor();

    // Check if pressure sensor is pressed (voltage drops below threshold)
    if (pressure_value < PRESSURE_SENSOR_PRESSED_THRESHOLD)
    {
      // Pressure sensor is pressed, stop and reset position to 0
      StepperMotor_Stop(motor);
      motor->current_position = 0;
      motor->target_position = 0;
      break;
    }

    // Move one step backward
    StepperMotor_MoveRelative(motor, -1);

    // Wait for step to complete
    while (StepperMotor_IsMoving(motor))
    {
      StepperMotor_Update(motor);
      HAL_Delay(1); // Small delay to prevent blocking
    }

    step_count++;

    // Small delay between steps for stability
    HAL_Delay(10);
  }

  // Restore original speed
  StepperMotor_SetSpeed(motor, original_speed);

  // If we hit the safety limit, reset position anyway
  if (step_count >= max_calibration_steps)
  {
    motor->current_position = 0;
    motor->target_position = 0;
  }
}

// Initialize stepper motor
void StepperMotor_Init(StepperMotor_t *motor)
{
  // Enable DWT counter for microsecond timing
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  motor->current_position = 0;
  motor->target_position = 0;
  motor->current_speed = STEPPER_MIN_SPEED_STEPS_PER_SEC;
  motor->direction = STEPPER_DIR_CW;
  motor->is_moving = 0;

  // Initialize GPIO pins
  HAL_GPIO_WritePin(STEPPER_STEP_PORT, STEPPER_STEP_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(STEPPER_DIR_PORT, STEPPER_DIR_PIN, GPIO_PIN_RESET);

  // Initialize ADC for pressure sensor
  StepperMotor_InitADC();

  // Calibrate position using pressure sensor
  StepperMotor_CalibratePosition(motor);
}

// Set stepper motor direction
void StepperMotor_SetDirection(StepperMotor_t *motor, StepperDirection_t direction)
{
  motor->direction = direction;
  HAL_GPIO_WritePin(STEPPER_DIR_PORT, STEPPER_DIR_PIN, direction ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// Move to absolute position
void StepperMotor_MoveTo(StepperMotor_t *motor, int32_t target_position)
{
  motor->target_position = target_position;
  motor->is_moving = 1;

  if (target_position > motor->current_position)
  {
    StepperMotor_SetDirection(motor, STEPPER_DIR_CW);
  }
  else if (target_position < motor->current_position)
  {
    StepperMotor_SetDirection(motor, STEPPER_DIR_CCW);
  }
  else
  {
    motor->is_moving = 0;
    return;
  }

  // Set step delay based on current speed
  step_delay_us = calculateStepDelay(motor->current_speed);
}

// Move relative steps
void StepperMotor_MoveRelative(StepperMotor_t *motor, int32_t steps)
{
  StepperMotor_MoveTo(motor, motor->current_position + steps);
}

// Stop stepper motor
void StepperMotor_Stop(StepperMotor_t *motor)
{
  motor->is_moving = 0;
  motor->target_position = motor->current_position;
}

// Set stepper motor speed
void StepperMotor_SetSpeed(StepperMotor_t *motor, uint32_t speed_steps_per_sec)
{
  if (speed_steps_per_sec > STEPPER_MAX_SPEED_STEPS_PER_SEC)
  {
    motor->current_speed = STEPPER_MAX_SPEED_STEPS_PER_SEC;
  }
  else if (speed_steps_per_sec < STEPPER_MIN_SPEED_STEPS_PER_SEC)
  {
    motor->current_speed = STEPPER_MIN_SPEED_STEPS_PER_SEC;
  }
  else
  {
    motor->current_speed = speed_steps_per_sec;
  }

  step_delay_us = calculateStepDelay(motor->current_speed);
}

// Non-blocking step pulse state machine
void StepperMotor_StepPulseUpdate(void)
{
  uint32_t current_time = getMicroseconds();

  switch (step_state)
  {
  case STEP_IDLE:
    // Nothing to do
    break;

  case STEP_PULSE_HIGH:
    // Check if high pulse time has elapsed (10μs minimum)
    if ((current_time - step_pulse_start_time) >= 10)
    {
      HAL_GPIO_WritePin(STEPPER_STEP_PORT, STEPPER_STEP_PIN, GPIO_PIN_RESET);
      step_state = STEP_PULSE_LOW;
      step_pulse_start_time = current_time;
    }
    break;

  case STEP_PULSE_LOW:
    // Check if low pulse time has elapsed (2.5μs minimum)
    if ((current_time - step_pulse_start_time) >= 3)
    {
      step_state = STEP_IDLE;
    }
    break;
  }
}

// Start a step pulse (non-blocking)
void StepperMotor_StartStepPulse(void)
{
  if (step_state == STEP_IDLE)
  {
    HAL_GPIO_WritePin(STEPPER_STEP_PORT, STEPPER_STEP_PIN, GPIO_PIN_SET);
    step_state = STEP_PULSE_HIGH;
    step_pulse_start_time = getMicroseconds();
  }
}

// Check if step pulse is complete
uint8_t StepperMotor_IsStepPulseComplete(void)
{
  return (step_state == STEP_IDLE);
}

// Update stepper motor (call this in main loop)
void StepperMotor_Update(StepperMotor_t *motor)
{
  // Update step pulse state machine
  StepperMotor_StepPulseUpdate();

  // Use microsecond timing for better accuracy
  uint32_t current_time = getMicroseconds();

  // Check if it's time for the next step
  if ((current_time - last_step_time) >= step_delay_us)
  {
    if (motor->is_moving && motor->current_position != motor->target_position)
    {
      // Only start a new step pulse if the previous one is complete
      if (StepperMotor_IsStepPulseComplete())
      {
        StepperMotor_StartStepPulse();

        // Update position
        if (motor->direction == STEPPER_DIR_CW)
        {
          motor->current_position++;
        }
        else
        {
          motor->current_position--;
        }

        last_step_time = current_time;

        // Check if we've completed all steps
        if (motor->current_position == motor->target_position)
        {
          motor->is_moving = 0;
        }
      }
    }
  }
}

// Check if motor is moving
uint8_t StepperMotor_IsMoving(StepperMotor_t *motor)
{
  return motor->is_moving;
}

// Get current position
int32_t StepperMotor_GetPosition(StepperMotor_t *motor)
{
  return motor->current_position;
}
