#include "command_parser.h"
#include "rs485.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// Global command queue
static CommandQueue_t g_command_queue;

// Note to semitone mapping within an octave (A=9, B=11, C=0, D=2, E=4, F=5, G=7)
static const int NOTE_TO_SEMITONE[] = {
    9,  // A (index 0)
    11, // B (index 1)
    0,  // C (index 2)
    2,  // D (index 3)
    4,  // E (index 4)
    5,  // F (index 5)
    7   // G (index 6)
};

HAL_StatusTypeDef CommandParser_ParseMessage(const char *message, uint16_t length, ParsedCommand_t *command)
{
  // Expected format: "P:C4:127", "P:C#4:127", "R:C4:0", etc. (octave is always 1 digit)
  if (message == NULL || command == NULL || length < 6) // Minimum length for "P:C4:0"
  {
    return HAL_ERROR;
  }

  // Parse command type (P or R)
  if (message[0] == 'P')
  {
    command->type = COMMAND_PRESS;
  }
  else if (message[0] == 'R')
  {
    command->type = COMMAND_RELEASE;
  }
  else
  {
    return HAL_ERROR; // Invalid command type
  }

  // Check for colon separator
  if (message[1] != ':')
  {
    return HAL_ERROR;
  }

  // Parse note (e.g., "C4" or "C#4")
  char note_char = toupper(message[2]);
  if (note_char < 'A' || note_char > 'G')
  {
    return HAL_ERROR; // Invalid note
  }

  command->note.note = note_char;

  // Check for sharp/flat and parse octave
  int octave_pos = 3;
  command->note.accidental = '\0'; // Default to natural
  if (octave_pos < length && (message[octave_pos] == '#' || message[octave_pos] == 'b'))
  {
    command->note.accidental = message[octave_pos];
    octave_pos++; // Skip sharp/flat
  }

  // Parse octave number (single digit)
  if (octave_pos >= length || !isdigit(message[octave_pos]))
  {
    return HAL_ERROR; // Invalid octave
  }

  command->note.octave = message[octave_pos] - '0';
  int i = octave_pos + 1;

  // Check for second colon separator
  if (i >= length || message[i] != ':')
  {
    return HAL_ERROR;
  }

  // Parse velocity
  i++; // Skip the colon
  int velocity = 0;
  while (i < length && isdigit(message[i]))
  {
    velocity = velocity * 10 + (message[i] - '0');
    i++;
  }

  // Validate velocity range
  if (velocity < 0 || velocity > 127)
  {
    return HAL_ERROR;
  }

  command->note.velocity = velocity;

  // Map note to channel (C4 octave maps to 12 channels)
  command->channel = CommandParser_MapNoteToChannel(&command->note);

  return HAL_OK;
}

uint8_t CommandParser_MapNoteToChannel(const NoteCommand_t *note)
{
  if (note == NULL)
  {
    return 0;
  }

  // Map note letter to semitone offset
  int note_index = note->note - 'A';
  if (note_index < 0 || note_index > 6)
  {
    return 0; // Invalid note, default to channel 0
  }

  // Start with base semitone offset
  int semitone_offset = NOTE_TO_SEMITONE[note_index];

  // Apply accidental
  if (note->accidental == '#')
  {
    semitone_offset += 1;
  }
  else if (note->accidental == 'b')
  {
    semitone_offset -= 1;
  }

  // Map to 12 channels (0-11) for C4 octave
  // C4 octave notes: C4(0), C#4(1), D4(2), D#4(3), E4(4), F4(5),
  //                  F#4(6), G4(7), G#4(8), A4(9), A#4(10), B4(11)
  if (note->octave == 4)
  {
    int channel = semitone_offset;
    if (channel >= 0 && channel < 12)
    {
      return channel;
    }
  }

  // For other octaves, map to the same note in C4 octave
  return semitone_offset % 12;
}

void CommandParser_ExecuteCommand(const ParsedCommand_t *command, KeyDriverModule_t *key_driver)
{
  if (command == NULL || key_driver == NULL)
  {
    return;
  }

  // Ensure channel is within valid range
  if (command->channel >= NUM_KEYS)
  {
    return;
  }

  if (command->type == COMMAND_PRESS)
  {
    KeyDriver_PressKey(key_driver, command->channel, command->note.velocity);
  }
  else if (command->type == COMMAND_RELEASE)
  {
    KeyDriver_ReleaseKey(key_driver, command->channel);
  }
}

// Internal callback function for RS485 messages
static void CommandParser_RS485Callback(const char *message, uint16_t length)
{
  if (message == NULL)
  {
    return;
  }

  // Format: "P:C4:127" or "R:C4:0"
  ParsedCommand_t parsed_command;
  if (CommandParser_ParseMessage(message, length, &parsed_command) == HAL_OK)
  {
    // Queue the parsed command for processing in main loop
    CommandQueue_Enqueue(&g_command_queue, &parsed_command);
  }
}

// Initialize the command parser module
void CommandParser_Init(KeyDriverModule_t *key_driver)
{
  if (key_driver == NULL)
  {
    return;
  }

  // Initialize the command queue
  CommandQueue_Init(&g_command_queue);

  // Set up the RS485 callback to use our internal handler
  RS485_SetMessageCallback(CommandParser_RS485Callback);
}

// ============================================================================
// Command Queue Management Functions
// ============================================================================

/**
 * @brief Initialize command queue
 * @param queue: Pointer to queue structure
 * @return HAL status
 */
HAL_StatusTypeDef CommandQueue_Init(CommandQueue_t *queue)
{
  if (queue == NULL)
  {
    return HAL_ERROR;
  }

  queue->head = 0;
  queue->tail = 0;
  queue->count = 0;

  return HAL_OK;
}

/**
 * @brief Add command to queue
 * @param queue: Pointer to queue structure
 * @param command: Pointer to command to enqueue
 * @return HAL status
 */
HAL_StatusTypeDef CommandQueue_Enqueue(CommandQueue_t *queue, const ParsedCommand_t *command)
{
  if (queue == NULL || command == NULL)
  {
    return HAL_ERROR;
  }

  // Check if queue is full
  if (CommandQueue_IsFull(queue))
  {
    return HAL_ERROR; // Queue overflow
  }

  // Add command to queue
  queue->commands[queue->tail] = *command;
  queue->tail = (queue->tail + 1) % COMMAND_QUEUE_SIZE;
  queue->count++;

  return HAL_OK;
}

/**
 * @brief Remove command from queue
 * @param queue: Pointer to queue structure
 * @param command: Pointer to store dequeued command
 * @return HAL status
 */
HAL_StatusTypeDef CommandQueue_Dequeue(CommandQueue_t *queue, ParsedCommand_t *command)
{
  if (queue == NULL || command == NULL)
  {
    return HAL_ERROR;
  }

  // Check if queue is empty
  if (CommandQueue_IsEmpty(queue))
  {
    return HAL_ERROR; // Queue empty
  }

  // Remove command from queue
  *command = queue->commands[queue->head];
  queue->head = (queue->head + 1) % COMMAND_QUEUE_SIZE;
  queue->count--;

  return HAL_OK;
}

/**
 * @brief Check if queue is empty
 * @param queue: Pointer to queue structure
 * @return 1 if empty, 0 if not empty
 */
uint8_t CommandQueue_IsEmpty(const CommandQueue_t *queue)
{
  if (queue == NULL)
  {
    return 1;
  }

  return (queue->count == 0);
}

/**
 * @brief Check if queue is full
 * @param queue: Pointer to queue structure
 * @return 1 if full, 0 if not full
 */
uint8_t CommandQueue_IsFull(const CommandQueue_t *queue)
{
  if (queue == NULL)
  {
    return 1;
  }

  return (queue->count >= COMMAND_QUEUE_SIZE);
}

/**
 * @brief Process all commands in the queue
 * @param queue: Pointer to queue structure
 * @param key_driver: Pointer to key driver module
 */
void CommandParser_ProcessQueue(CommandQueue_t *queue, KeyDriverModule_t *key_driver)
{
  if (queue == NULL || key_driver == NULL)
  {
    return;
  }

  // Process all commands in the queue
  while (!CommandQueue_IsEmpty(queue))
  {
    ParsedCommand_t command;
    if (CommandQueue_Dequeue(queue, &command) == HAL_OK)
    {
      CommandParser_ExecuteCommand(&command, key_driver);
    }
  }
}

/**
 * @brief Get pointer to global command queue (for external access)
 * @return Pointer to global command queue
 */
CommandQueue_t *CommandParser_GetQueue(void)
{
  return &g_command_queue;
}
