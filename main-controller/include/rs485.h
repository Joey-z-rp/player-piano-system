#ifndef RS485_H
#define RS485_H

#include <Arduino.h>
#include <HardwareSerial.h>

static const int RS485_TX_PIN = 17;
static const uint32_t RS485_BAUD = 115200;

class Rs485Module
{
public:
  Rs485Module();

  bool init();
  bool send(const char *command);

private:
  HardwareSerial serial;
  bool started;
};

#endif // RS485_H
