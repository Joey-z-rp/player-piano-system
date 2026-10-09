#include "app/piano_controller.h"
#include "rs485.h"

PianoController::PianoController() : rs485(nullptr), commandQueue(nullptr)
{
}

bool PianoController::init(Rs485Module *rs485)
{
  this->rs485 = rs485;
  commandQueue = xQueueCreate(DRIVER_COMMAND_QUEUE_LENGTH, sizeof(DriverCommand));
  return commandQueue != nullptr;
}

SubmitResult PianoController::submitDriverCommand(const String &command)
{
  if (!isDriverCommand(command))
  {
    return SubmitResult::Invalid;
  }

  DriverCommand item;
  strlcpy(item.text, command.c_str(), sizeof(item.text));
  if (commandQueue == nullptr || xQueueSend(commandQueue, &item, 0) != pdTRUE)
  {
    return SubmitResult::QueueFull;
  }
  return SubmitResult::Queued;
}

size_t PianoController::pendingCommands() const
{
  return commandQueue == nullptr ? 0 : uxQueueMessagesWaiting(commandQueue);
}

void PianoController::process()
{
  if (commandQueue == nullptr)
  {
    return;
  }

  DriverCommand item;
  while (xQueueReceive(commandQueue, &item, 0) == pdTRUE)
  {
    if (rs485 == nullptr || !rs485->send(item.text))
    {
      Serial.printf("RS485 send failed: %s\n", item.text);
      continue;
    }
    Serial.printf("RS485 sent: %s\n", item.text);
  }
}

bool PianoController::isDriverCommand(const String &command)
{
  if (command.length() < 3 || command.length() > DRIVER_COMMAND_MAX_LENGTH)
  {
    return false;
  }
  if (!command.startsWith("P:") && !command.startsWith("R:"))
  {
    return false;
  }
  for (unsigned int i = 2; i < command.length(); i++)
  {
    char c = command.charAt(i);
    if (!isdigit(c) && c != ':')
    {
      return false;
    }
  }
  return true;
}
