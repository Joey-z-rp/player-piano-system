#ifndef OTA_UPDATES_H
#define OTA_UPDATES_H

#include <stdint.h>

class PianoController;

// Updates over Wi-Fi (pio run -e ota -t upload / -t uploadfs), with
// secrets.ini's ota_password; without one, they're turned off. An update
// stops whatever's playing first, and Play waits until the controller has
// restarted into it. The web files and velocity table go to the "spiffs"
// partition, never the library.
class OtaUpdates
{
public:
  enum class Kind : uint8_t
  {
    None,
    Firmware,
    Files // the web files and velocity table
  };

  bool begin(PianoController &piano);
  void poll(); // from loop()

  Kind updating() const;
  uint8_t percent() const;

private:
  PianoController *piano = nullptr;
  volatile Kind kind = Kind::None;
  volatile uint8_t done = 0;
};

#endif // OTA_UPDATES_H
