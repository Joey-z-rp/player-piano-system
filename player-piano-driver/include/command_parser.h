#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include "stm32f1xx_hal.h"
#include "key_driver.h"

// Command queue configuration
#define COMMAND_QUEUE_SIZE 64 // holds one less: a slot stays empty to tell full from empty

// Command types
typedef enum
{
  COMMAND_PRESS = 0,
  COMMAND_RELEASE
} CommandType_t;

// Parsed command structure
typedef struct
{
  CommandType_t type;
  uint8_t channel;              // Piano key (0-87) while parsing; local PWM index once queued
  // Duty cycles in hundredths of a percent (0-PWM_DUTY_FULL).
  uint16_t duty_cycle;          // Initial duty cycle
  uint16_t initial_strike_time; // Initial strike time in ms (0 = use default)
  KeyPhase_t phases[MAX_PHASES]; // between the strike and the hold (a 0 duty or time skips one)
  uint8_t phase_count;
  uint16_t hold_duty_cycle;     // Hold duty (DUTY_OMITTED = use default)
} ParsedCommand_t;

// Parsed commands pass from RS485_Poll (Enqueue, which only moves tail) to
// the key driver's update (Dequeue, which only moves head), both in the main
// loop; neither side writes the other's index.
typedef struct
{
  ParsedCommand_t commands[COMMAND_QUEUE_SIZE];
  volatile uint8_t head;
  volatile uint8_t tail;
} CommandQueue_t;

// Function prototypes
HAL_StatusTypeDef CommandParser_ParseMessage(const char *message, uint16_t length, ParsedCommand_t *command);
void CommandParser_ExecuteCommand(const ParsedCommand_t *command, KeyDriverModule_t *key_driver);
void CommandParser_Init(KeyDriverModule_t *key_driver);

// Queue management functions
HAL_StatusTypeDef CommandQueue_Init(CommandQueue_t *queue);
HAL_StatusTypeDef CommandQueue_Enqueue(CommandQueue_t *queue, const ParsedCommand_t *command);
HAL_StatusTypeDef CommandQueue_Dequeue(CommandQueue_t *queue, ParsedCommand_t *command);
uint8_t CommandQueue_IsEmpty(const CommandQueue_t *queue);
uint8_t CommandQueue_IsFull(const CommandQueue_t *queue);
void CommandParser_ProcessQueue(CommandQueue_t *queue, KeyDriverModule_t *key_driver);
CommandQueue_t *CommandParser_GetQueue(void);

#endif // COMMAND_PARSER_H
