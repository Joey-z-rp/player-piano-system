#include "command_parser.h"
#include "rs485.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// Global command queue
static CommandQueue_t g_command_queue;

// No note mapping needed for direct channel/duty cycle format

HAL_StatusTypeDef CommandParser_ParseMessage(const char *message, uint16_t length, ParsedCommand_t *command)
{
  // Expected formats:
  // "P:0:100" - channel 0, duty cycle 100, default timing
  // "P:0:100:50" - channel 0, duty cycle 100, initial strike time 50ms
  // "P:0:100:50:80:100" - channel 0, duty cycle 100, initial strike 50ms, follow-up duty 80, follow-up time 100ms
  // "R:0:0" - release channel 0
  if (message == NULL || command == NULL || length < 5) // Minimum length for "P:0:0"
  {
    return HAL_ERROR;
  }

  // Initialize command with defaults
  command->initial_strike_time = 0; // 0 means use default
  command->followup_duty_cycle = 0; // 0 means no follow-up
  command->followup_time = 0;       // 0 means no follow-up

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

  // Parse channel (0-11)
  int i = 2;
  int channel = 0;
  while (i < length && isdigit(message[i]))
  {
    channel = channel * 10 + (message[i] - '0');
    i++;
  }

  // Validate channel range
  if (channel < 0 || channel > 11)
  {
    return HAL_ERROR;
  }

  command->channel = channel;

  // Check for second colon separator
  if (i >= length || message[i] != ':')
  {
    return HAL_ERROR;
  }

  // Parse initial duty cycle
  i++; // Skip the colon
  int duty_cycle = 0;
  while (i < length && isdigit(message[i]))
  {
    duty_cycle = duty_cycle * 10 + (message[i] - '0');
    i++;
  }

  // Validate duty cycle range
  if (duty_cycle < 0 || duty_cycle > 100)
  {
    return HAL_ERROR;
  }

  command->duty_cycle = duty_cycle;

  // For release commands, we're done
  if (command->type == COMMAND_RELEASE)
  {
    return HAL_OK;
  }

  // Parse optional parameters for press commands
  while (i < length && message[i] == ':')
  {
    i++; // Skip the colon

    // Parse next parameter
    int param_value = 0;
    while (i < length && isdigit(message[i]))
    {
      param_value = param_value * 10 + (message[i] - '0');
      i++;
    }

    // Determine which parameter this is based on current state
    if (command->initial_strike_time == 0) // First optional parameter
    {
      command->initial_strike_time = param_value;
    }
    else if (command->followup_duty_cycle == 0) // Second optional parameter
    {
      command->followup_duty_cycle = param_value;
    }
    else if (command->followup_time == 0) // Third optional parameter
    {
      command->followup_time = param_value;
    }
    else
    {
      return HAL_ERROR; // Too many parameters
    }
  }

  // Validate follow-up duty cycle if specified
  if (command->followup_duty_cycle < 0 || command->followup_duty_cycle > 100)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
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
    KeyDriver_PressKey(key_driver, command->channel, command->duty_cycle,
                       command->initial_strike_time, command->followup_duty_cycle,
                       command->followup_time);
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

  // Format: "P:11:100" or "R:11:0"
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
