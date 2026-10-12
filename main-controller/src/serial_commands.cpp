#include "serial_commands.h"
#include "app/piano/piano_controller.h"

SerialCommands::SerialCommands() : piano(nullptr), length(0), overflow(false)
{
}

void SerialCommands::begin(PianoController *piano)
{
  this->piano = piano;
}

void SerialCommands::poll()
{
  while (Serial.available() > 0)
  {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r')
    {
      if (overflow)
      {
        Serial.println("#ERR line too long");
      }
      else if (length > 0)
      {
        line[length] = '\0';
        handleLine();
      }
      length = 0;
      overflow = false;
      continue;
    }
    if (length < sizeof(line) - 1)
    {
      line[length++] = c;
    }
    else
    {
      overflow = true;
    }
  }
}

void SerialCommands::handleLine()
{
  if (piano == nullptr)
  {
    Serial.println("#ERR not ready");
    return;
  }

  String command(line);
  command.trim();

  if (command == "STOP")
  {
    Serial.println(piano->stop() == PlaybackResult::Ok ? "#OK" : "#ERR queue full");
    return;
  }
  if (command == "RESET")
  {
    Serial.println(piano->resetDrivers() == PlaybackResult::Ok ? "#OK" : "#ERR queue full");
    return;
  }

  switch (piano->submitDriverCommand(command))
  {
  case SubmitResult::Queued:
    Serial.println("#OK");
    break;
  case SubmitResult::Invalid:
    Serial.println("#ERR invalid command");
    break;
  case SubmitResult::QueueFull:
    Serial.println("#ERR queue full");
    break;
  }
}
