#include "stepper_motor.h"
#include "core_cm3.h" // For DWT registers

// Global stepper motor instance
StepperMotor_t g_stepper_motor;

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
}

// Set stepper motor direction
void StepperMotor_SetDirection(StepperMotor_t *motor, StepperDirection_t direction)
{
  motor->direction = direction;
  HAL_GPIO_WritePin(STEPPER_DIR_PORT, STEPPER_DIR_PIN, direction ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// Move to absolute position
void StepperMotor_MoveTo(StepperMotor_t *motor, uint32_t target_position)
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
  if (steps >= 0)
  {
    StepperMotor_MoveTo(motor, motor->current_position + steps);
  }
  else
  {
    StepperMotor_MoveTo(motor, motor->current_position - (-steps));
  }
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
uint32_t StepperMotor_GetPosition(StepperMotor_t *motor)
{
  return motor->current_position;
}
