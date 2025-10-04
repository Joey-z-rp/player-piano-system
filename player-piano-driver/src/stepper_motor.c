#include "stepper_motor.h"

// Global stepper motor instance
StepperMotor_t g_stepper_motor;

// Private variables for timing
static uint32_t step_delay_us = 1000; // Default delay
static uint32_t last_step_time = 0;

// Accurate microsecond delay function
void delayMicroseconds(uint32_t us)
{
  // Simple delay for STM32F1 (72MHz)
  uint32_t delay_cycles = us * 18; // 72MHz / 4 = 18 cycles per microsecond
  volatile uint32_t count = 0;
  while (count < delay_cycles)
  {
    count++;
  }
}

// Calculate step delay based on speed
uint32_t calculateStepDelay(uint32_t speed_steps_per_sec)
{
  if (speed_steps_per_sec == 0)
    return 1000000;                     // 1 second delay if speed is 0
  return 1000000 / speed_steps_per_sec; // Convert to microseconds
}

// Initialize stepper motor
void StepperMotor_Init(StepperMotor_t *motor)
{
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

// Execute a single step
void StepperMotor_Step(StepperMotor_t *motor)
{
  // Generate step pulse (minimum 2.5μs high, 2.5μs low for TB6600)
  HAL_GPIO_WritePin(STEPPER_STEP_PORT, STEPPER_STEP_PIN, GPIO_PIN_SET);
  delayMicroseconds(10); // 10μs high pulse for better reliability
  HAL_GPIO_WritePin(STEPPER_STEP_PORT, STEPPER_STEP_PIN, GPIO_PIN_RESET);

  // Update position
  if (motor->direction == STEPPER_DIR_CW)
  {
    motor->current_position++;
  }
  else
  {
    motor->current_position--;
  }
}

// Update stepper motor (call this in main loop)
void StepperMotor_Update(StepperMotor_t *motor)
{
  // Use millisecond timing for simplicity
  uint32_t current_time = HAL_GetTick();
  uint32_t step_delay_ms = step_delay_us / 1000; // Convert microseconds to milliseconds

  // Check if it's time for the next step
  if ((current_time - last_step_time) >= step_delay_ms)
  {
    if (motor->is_moving && motor->current_position != motor->target_position)
    {
      StepperMotor_Step(motor);
      last_step_time = current_time;

      // Check if we've completed all steps
      if (motor->current_position == motor->target_position)
      {
        motor->is_moving = 0;
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
