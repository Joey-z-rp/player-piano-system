#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include "stm32f1xx_hal.h"
#include "key_driver.h"

// Command queue configuration
#define COMMAND_QUEUE_SIZE 32

// Command types
typedef enum
{
  COMMAND_PRESS = 0,
  COMMAND_RELEASE
} CommandType_t;

// Note structure
typedef struct
{
  char note;       // 'C', 'D', 'E', 'F', 'G', 'A', 'B'
  char accidental; // '#', 'b', or '\0' for natural
  int octave;      // Octave number (e.g., 4 for C4)
  int velocity;    // 0-127
} NoteCommand_t;

// Parsed command structure
typedef struct
{
  CommandType_t type;
  NoteCommand_t note;
  uint8_t channel; // Mapped channel (0-11 for 12 channels)
} ParsedCommand_t;

// Command queue structure
typedef struct
{
  ParsedCommand_t commands[COMMAND_QUEUE_SIZE];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
} CommandQueue_t;

// Function prototypes
HAL_StatusTypeDef CommandParser_ParseMessage(const char *message, uint16_t length, ParsedCommand_t *command);
uint8_t CommandParser_MapNoteToChannel(const NoteCommand_t *note);
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
