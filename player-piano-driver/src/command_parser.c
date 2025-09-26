#include "command_parser.h"
#include "rs485.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

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
  // Get the key driver instance from the module's internal state
  extern KeyDriverModule_t g_key_driver;

  if (message == NULL || &g_key_driver == NULL)
  {
    return;
  }

  // Format: "P:C4:127" or "R:C4:0"
  ParsedCommand_t parsed_command;
  if (CommandParser_ParseMessage(message, length, &parsed_command) == HAL_OK)
  {
    // Execute the parsed command
    CommandParser_ExecuteCommand(&parsed_command, &g_key_driver);
    return;
  }
}

// Initialize the command parser module
void CommandParser_Init(KeyDriverModule_t *key_driver)
{
  if (key_driver == NULL)
  {
    return;
  }

  // Set up the RS485 callback to use our internal handler
  RS485_SetMessageCallback(CommandParser_RS485Callback);
}
