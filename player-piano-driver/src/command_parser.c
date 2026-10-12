#include "command_parser.h"
#include "board_config.h"
#include "rs485.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// Global command queue
static CommandQueue_t g_command_queue;

// Reads a duty cycle in percent with up to two decimals ("32", "32.5",
// "32.25") as hundredths of a percent, 0-PWM_DUTY_FULL.
static int ParseDuty(const char *message, uint16_t length, uint16_t *i, uint16_t *duty)
{
  uint32_t value = 0;
  uint8_t digits = 0;
  while (*i < length && isdigit((unsigned char)message[*i]))
  {
    value = value * 10 + (uint32_t)(message[*i] - '0');
    (*i)++;
    if (++digits > 3)
    {
      return 0;
    }
  }
  if (digits == 0)
  {
    return 0;
  }
  value *= 100;
  if (*i < length && message[*i] == '.')
  {
    (*i)++;
    uint32_t scale = 10;
    uint8_t decimals = 0;
    while (*i < length && isdigit((unsigned char)message[*i]))
    {
      if (++decimals > 2)
      {
        return 0;
      }
      value += (uint32_t)(message[*i] - '0') * scale;
      scale /= 10;
      (*i)++;
    }
    if (decimals == 0)
    {
      return 0;
    }
  }
  if (value > PWM_DUTY_FULL)
  {
    return 0;
  }
  *duty = (uint16_t)value;
  return 1;
}

// Reads a whole number up to 65535 (a key or a time in ms).
static int ParseNumber(const char *message, uint16_t length, uint16_t *i, uint16_t *number)
{
  uint32_t value = 0;
  uint8_t digits = 0;
  while (*i < length && isdigit((unsigned char)message[*i]))
  {
    value = value * 10 + (uint32_t)(message[*i] - '0');
    (*i)++;
    if (++digits > 5 || value > 65535)
    {
      return 0;
    }
  }
  if (digits == 0)
  {
    return 0;
  }
  *number = (uint16_t)value;
  return 1;
}

HAL_StatusTypeDef CommandParser_ParseMessage(const char *message, uint16_t length, ParsedCommand_t *command)
{
  // Formats (duty cycles in percent, with up to two decimals; times in ms):
  //   P:<key>:<duty>[:<strike time>[:<follow-up duty>:<follow-up time>[:<hold duty>]]]
  //   R:<key>[:<anything>]
  // e.g. "P:40:32.25", "P:88:100:30:100:30:55", "R:40:0".
  // Optional fields go by position: a strike time of 0 uses the default, a
  // follow-up duty or time of 0 skips the follow-up, and a hold of 0 is 0%.
  // Keys 0-87 are piano keys; 88 and 89 are board 0's pedal outputs.
  if (message == NULL || command == NULL || length < 3 || message[1] != ':')
  {
    return HAL_ERROR;
  }

  command->initial_strike_time = 0;
  command->followup_duty_cycle = 0;
  command->followup_time = 0;
  command->hold_duty_cycle = DUTY_OMITTED;
  command->duty_cycle = 0;

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
    return HAL_ERROR;
  }

  uint16_t i = 2;
  uint16_t channel;
  if (!ParseNumber(message, length, &i, &channel) ||
      (channel >= PIANO_NUM_KEYS && channel != PEDAL_KEY_A && channel != PEDAL_KEY_B))
  {
    return HAL_ERROR;
  }
  command->channel = (uint8_t)channel;

  if (command->type == COMMAND_RELEASE)
  {
    return HAL_OK; // anything after the key is ignored
  }

  if (i >= length || message[i] != ':')
  {
    return HAL_ERROR;
  }
  i++;
  if (!ParseDuty(message, length, &i, &command->duty_cycle))
  {
    return HAL_ERROR;
  }

  for (uint8_t field = 0; i < length && message[i] == ':'; field++)
  {
    i++;
    int ok;
    switch (field)
    {
    case 0:
      ok = ParseNumber(message, length, &i, &command->initial_strike_time);
      break;
    case 1:
      ok = ParseDuty(message, length, &i, &command->followup_duty_cycle);
      break;
    case 2:
      ok = ParseNumber(message, length, &i, &command->followup_time);
      break;
    case 3:
      ok = ParseDuty(message, length, &i, &command->hold_duty_cycle);
      break;
    default:
      ok = 0; // too many fields
    }
    if (!ok)
    {
      return HAL_ERROR;
    }
  }

  // Allow a trailing carriage return or spaces, nothing else.
  while (i < length && (message[i] == '\r' || message[i] == ' '))
  {
    i++;
  }
  return i == length ? HAL_OK : HAL_ERROR;
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
                       command->followup_time, command->hold_duty_cycle);
  }
  else if (command->type == COMMAND_RELEASE)
  {
    KeyDriver_ReleaseKey(key_driver, command->channel);
  }
}

// "RESET:ALL", or "RESET:<n>" for board n: a safety net that restarts the
// driver from any bad state. The controller sends it before each song.
static int IsResetForThisBoard(const char *message, uint16_t length)
{
  static const char prefix[] = "RESET:";
  const uint16_t prefix_length = sizeof(prefix) - 1;
  while (length > 0 && (message[length - 1] == '\r' || message[length - 1] == ' '))
  {
    length--;
  }
  if (length <= prefix_length || strncmp(message, prefix, prefix_length) != 0)
  {
    return 0;
  }
  const char *target = message + prefix_length;
  uint16_t target_length = length - prefix_length;
  if (target_length == 3 && strncmp(target, "ALL", 3) == 0)
  {
    return 1;
  }
  return target_length == 1 && target[0] == '0' + BOARD_NUMBER;
}

// Internal callback function for RS485 messages
static void CommandParser_RS485Callback(const char *message, uint16_t length)
{
  if (message == NULL)
  {
    return;
  }

  if (IsResetForThisBoard(message, length))
  {
    // A full restart; the PWM outputs switch off with it, so every solenoid lets go.
    NVIC_SystemReset();
  }

  // Format: "P:42:100" or "R:42:0" where the number is a piano key (0-87)
  ParsedCommand_t parsed_command;
  if (CommandParser_ParseMessage(message, length, &parsed_command) == HAL_OK)
  {
    if (parsed_command.type == COMMAND_PRESS || parsed_command.type == COMMAND_RELEASE)
    {
      if (parsed_command.channel == PEDAL_KEY_A || parsed_command.channel == PEDAL_KEY_B)
      {
        if (PEDAL_LOCAL_CHANNELS == 0)
        {
          return;
        }
        parsed_command.channel = (uint8_t)(LOCAL_CHANNELS + (parsed_command.channel - PEDAL_KEY_A));
      }
      else if (parsed_command.channel < KEY_BASE ||
               parsed_command.channel >= (KEY_BASE + LOCAL_CHANNELS))
      {
        return;
      }
      else
      {
        parsed_command.channel = (uint8_t)(parsed_command.channel - KEY_BASE);
      }
    }

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

  return HAL_OK;
}

/**
 * @brief Add command to queue. Called only from the serial interrupt.
 * @param queue: Pointer to queue structure
 * @param command: Pointer to command to enqueue
 * @return HAL status (HAL_ERROR when full: the command is dropped)
 */
HAL_StatusTypeDef CommandQueue_Enqueue(CommandQueue_t *queue, const ParsedCommand_t *command)
{
  if (queue == NULL || command == NULL)
  {
    return HAL_ERROR;
  }

  uint8_t next = (uint8_t)((queue->tail + 1) % COMMAND_QUEUE_SIZE);
  if (next == queue->head)
  {
    return HAL_ERROR; // full
  }

  queue->commands[queue->tail] = *command;
  __DMB(); // the command is fully written before the main loop can see it
  queue->tail = next;

  return HAL_OK;
}

/**
 * @brief Remove command from queue. Called only from the main loop.
 * @param queue: Pointer to queue structure
 * @param command: Pointer to store dequeued command
 * @return HAL status (HAL_ERROR when empty)
 */
HAL_StatusTypeDef CommandQueue_Dequeue(CommandQueue_t *queue, ParsedCommand_t *command)
{
  if (queue == NULL || command == NULL)
  {
    return HAL_ERROR;
  }

  uint8_t head = queue->head;
  if (head == queue->tail)
  {
    return HAL_ERROR; // empty
  }

  __DMB(); // read the command only after seeing it was queued
  *command = queue->commands[head];
  __DMB(); // finish reading before handing the slot back to the interrupt
  queue->head = (uint8_t)((head + 1) % COMMAND_QUEUE_SIZE);

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

  return queue->head == queue->tail;
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

  return (uint8_t)((queue->tail + 1) % COMMAND_QUEUE_SIZE) == queue->head;
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
