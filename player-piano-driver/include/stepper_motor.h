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
#define STEPPER_MAX_SPEED_STEPS_PER_SEC 2500
#define STEPPER_MIN_SPEED_STEPS_PER_SEC 500

// Position definitions
#define STEPPER_POSITION_IDLE 300           // Idle position after calibration
#define STEPPER_POSITION_PEDAL_PRESSED 1300 // Pedal pressed position
#define STEPPER_POSITION_PEDAL_RELEASED 600 // Pedal released position

// Timeout definitions
#define STEPPER_IDLE_TIMEOUT_MS 15000 // 15 seconds timeout for idle after pedal released

// Queue definitions
#define STEPPER_QUEUE_SIZE 8                // Maximum number of queued commands
#define STEPPER_MIN_COMMAND_INTERVAL_MS 150 // Minimum 150ms between command executions

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

// Command types for queue
typedef enum
{
  STEPPER_CMD_MOVE_TO = 0,           // Move to absolute position
  STEPPER_CMD_MOVE_RELATIVE,         // Move relative steps
  STEPPER_CMD_MOVE_TO_IDLE,          // Move to idle position
  STEPPER_CMD_MOVE_TO_PEDAL_PRESSED, // Move to pedal pressed position
  STEPPER_CMD_MOVE_TO_PEDAL_RELEASED // Move to pedal released position
} StepperCommandType_t;

// Command structure for queue
typedef struct
{
  StepperCommandType_t type; // Command type
  int32_t value;             // Command value (position, steps, speed)
} StepperCommand_t;

// Queue structure
typedef struct
{
  StepperCommand_t commands[STEPPER_QUEUE_SIZE]; // Command buffer
  uint8_t head;                                  // Index of next command to execute
  uint8_t tail;                                  // Index of next free slot
  uint8_t count;                                 // Number of commands in queue
  uint32_t last_command_time;                    // Timestamp of last executed command
} StepperQueue_t;

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
  StepperQueue_t command_queue;    // Command queue for queued operations
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

// Direct execution functions (called from queue processor)
void StepperMotor_MoveToDirect(StepperMotor_t *motor, int32_t target_position);
void StepperMotor_MoveRelativeDirect(StepperMotor_t *motor, int32_t steps);
void StepperMotor_MoveToIdleDirect(StepperMotor_t *motor);
void StepperMotor_MoveToPedalPressedDirect(StepperMotor_t *motor);
void StepperMotor_MoveToPedalReleasedDirect(StepperMotor_t *motor);

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

// Queue management functions
void StepperMotor_QueueInit(StepperQueue_t *queue);
uint8_t StepperMotor_QueueEnqueue(StepperQueue_t *queue, StepperCommandType_t type, int32_t value);
uint8_t StepperMotor_QueueDequeue(StepperQueue_t *queue, StepperCommand_t *command);
uint8_t StepperMotor_QueueIsEmpty(StepperQueue_t *queue);
uint8_t StepperMotor_QueueIsFull(StepperQueue_t *queue);
void StepperMotor_ProcessQueue(StepperMotor_t *motor);

#endif // STEPPER_MOTOR_H
