#ifndef STEPPER_MOTOR_H
#define STEPPER_MOTOR_H

#include "stm32f1xx_hal.h"

// Stepper motor pin definitions
#define STEPPER_STEP_PIN GPIO_PIN_14
#define STEPPER_STEP_PORT GPIOC
#define STEPPER_DIR_PIN GPIO_PIN_15
#define STEPPER_DIR_PORT GPIOC

// Pressure sensor definitions
#define PRESSURE_SENSOR_PIN GPIO_PIN_4
#define PRESSURE_SENSOR_PORT GPIOA
#define PRESSURE_SENSOR_ADC_CHANNEL ADC_CHANNEL_4
#define PRESSURE_SENSOR_ADC ADC1

// Pressure sensor thresholds (12-bit ADC values)
#define PRESSURE_SENSOR_PRESSED_THRESHOLD 3700 // ~3.0V (4095 * 3.0 / 3.3)

// Speed limits
#define STEPPER_MAX_SPEED_STEPS_PER_SEC 1500
#define STEPPER_MIN_SPEED_STEPS_PER_SEC 100

// Position definitions
#define STEPPER_POSITION_IDLE 100           // Idle position after calibration
#define STEPPER_POSITION_PEDAL_PRESSED 250  // Pedal pressed position
#define STEPPER_POSITION_PEDAL_RELEASED 150 // Pedal released position

// Timeout definitions
#define STEPPER_IDLE_TIMEOUT_MS 15000 // 15 seconds timeout for idle after pedal released

// Direction definitions
typedef enum
{
  STEPPER_DIR_CW = 0, // Clockwise
  STEPPER_DIR_CCW = 1 // Counter-clockwise
} StepperDirection_t;

// Last action definitions
typedef enum
{
  STEPPER_ACTION_NONE = 0,      // No action yet
  STEPPER_ACTION_PEDAL_PRESSED, // Last action was pedal pressed
  STEPPER_ACTION_PEDAL_RELEASED // Last action was pedal released
} StepperLastAction_t;

// Stepper motor state
typedef struct
{
  int32_t current_position;        // Current step position (can be negative)
  int32_t target_position;         // Target step position (can be negative)
  uint32_t current_speed;          // Current speed in steps per second
  StepperDirection_t direction;    // Current direction
  uint8_t is_moving;               // Motor movement state
  StepperLastAction_t last_action; // Last action performed
  uint32_t last_action_time;       // Timestamp of last action (in ms)
} StepperMotor_t;

// Function declarations
void StepperMotor_Init(StepperMotor_t *motor);
void StepperMotor_SetDirection(StepperMotor_t *motor, StepperDirection_t direction);
void StepperMotor_MoveTo(StepperMotor_t *motor, int32_t target_position);
void StepperMotor_MoveRelative(StepperMotor_t *motor, int32_t steps);
void StepperMotor_Stop(StepperMotor_t *motor);
void StepperMotor_Update(StepperMotor_t *motor);
uint8_t StepperMotor_IsMoving(StepperMotor_t *motor);
int32_t StepperMotor_GetPosition(StepperMotor_t *motor);

// Position-specific functions
void StepperMotor_MoveToIdle(StepperMotor_t *motor);
void StepperMotor_MoveToPedalPressed(StepperMotor_t *motor);
void StepperMotor_MoveToPedalReleased(StepperMotor_t *motor);

// Timeout management functions
void StepperMotor_CheckIdleTimeout(StepperMotor_t *motor);

// Low-level step functions
void StepperMotor_SetSpeed(StepperMotor_t *motor, uint32_t speed_steps_per_sec);

// Non-blocking step pulse functions
void StepperMotor_StepPulseUpdate(void);
void StepperMotor_StartStepPulse(void);
uint8_t StepperMotor_IsStepPulseComplete(void);

// ADC function
uint16_t StepperMotor_ReadPressureSensor(void);

#endif // STEPPER_MOTOR_H
