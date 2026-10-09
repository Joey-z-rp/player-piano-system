#include "rs485.h"

Rs485Module::Rs485Module() : serial(1), started(false)
{
}

bool Rs485Module::init()
{
  serial.begin(RS485_BAUD, SERIAL_8N1, -1, RS485_TX_PIN);
  started = true;
  Serial.printf("RS485 ready on TX %d at %lu\n", RS485_TX_PIN, RS485_BAUD);
  return true;
}

bool Rs485Module::send(const char *command)
{
  if (!started || command == nullptr || command[0] == '\0')
  {
    return false;
  }

  serial.print(command);
  serial.print('\n');
  serial.flush();
  return true;
}
