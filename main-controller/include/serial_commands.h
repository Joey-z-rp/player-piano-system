#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <Arduino.h>

class PianoController;
class WiFiStationModule;

// Accepts driver commands over the USB serial port, one per line, for tools
// that need a more reliable link than Wi-Fi (tools/calibrate_dynamics.py):
//   P:<key>:<duty>...  R:<key>:0   queue a driver command
//   STOP                           release every key
//   RESET                          restart every driver board (stops any song)
//   CONFIG, OFFSETS                the play config and key offsets, as JSON (#CONFIG/#OFFSETS lines)
//   WIFI "<network>" "<password>"  save the Wi-Fi to join, and restart to join it
//   WIFI                           which network it's on, and which it joins (a #WIFI line)
//   WIFI FORGET                    forget the saved network
// Each line gets exactly one reply: "#OK" or "#ERR <reason>". Log output on
// the same port never starts with '#'.
class SerialCommands
{
public:
  SerialCommands();

  void begin(PianoController *piano, WiFiStationModule *wifi);

  // Call from loop().
  void poll();

private:
  void handleLine();
  void handleWifi(const String &arguments);

  PianoController *piano;
  WiFiStationModule *wifi;
  char line[160]; // a WIFI line with the longest network name and password
  size_t length;
  bool overflow;
};

#endif // SERIAL_COMMANDS_H
