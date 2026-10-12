#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <Arduino.h>

class PianoController;

// Accepts driver commands over the USB serial port, one per line, for tools
// that need a more reliable link than Wi-Fi (tools/calibrate_dynamics.py):
//   P:<key>:<duty>...  R:<key>:0   queue a driver command
//   STOP                           release every key
//   RESET                          restart every driver board (stops any song)
// Each line gets exactly one reply: "#OK" or "#ERR <reason>". Log output on
// the same port never starts with '#'.
class SerialCommands
{
public:
  SerialCommands();

  void begin(PianoController *piano);

  // Call from loop().
  void poll();

private:
  void handleLine();

  PianoController *piano;
  char line[48];
  size_t length;
  bool overflow;
};

#endif // SERIAL_COMMANDS_H
