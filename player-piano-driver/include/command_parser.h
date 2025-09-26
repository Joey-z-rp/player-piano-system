#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include "stm32f1xx_hal.h"
#include "key_driver.h"

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

// Function prototypes
HAL_StatusTypeDef CommandParser_ParseMessage(const char *message, uint16_t length, ParsedCommand_t *command);
uint8_t CommandParser_MapNoteToChannel(const NoteCommand_t *note);
void CommandParser_ExecuteCommand(const ParsedCommand_t *command, KeyDriverModule_t *key_driver);
void CommandParser_Init(KeyDriverModule_t *key_driver);

#endif // COMMAND_PARSER_H
