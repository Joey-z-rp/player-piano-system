#include "ota_updates.h"
#include "app/piano/piano_controller.h"
#include "wifi_station.h"
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

bool OtaUpdates::begin(PianoController &piano)
{
  this->piano = &piano;
#ifdef OTA_PASSWORD
  ArduinoOTA.setHostname(WiFiStationModule::HOSTNAME);
  ArduinoOTA.setMdnsEnabled(false); // the Wi-Fi module answers for piano.local (it can only be started once)
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.setPartitionLabel("spiffs"); // else it'd be the first such: the library
  ArduinoOTA.onStart([this]() {
    kind = ArduinoOTA.getCommand() == U_FLASH ? Kind::Firmware : Kind::Files;
    done = 0;
    this->piano->beginUpdate(); // stops it, every key and the pedal let go
    Serial.printf("Updating the %s over Wi-Fi\n", kind == Kind::Firmware ? "firmware" : "web files");
  });
  ArduinoOTA.onProgress([this](unsigned int written, unsigned int total) {
    done = total > 0 ? (uint8_t)(written * 100ULL / total) : 0;
  });
  ArduinoOTA.onEnd([]() { Serial.println("Update received: restarting into it"); });
  ArduinoOTA.onError([this](ota_error_t error) {
    Serial.printf("Update failed (%u): carrying on as before\n", (unsigned)error);
    kind = Kind::None;
    this->piano->endUpdate();
  });
  ArduinoOTA.begin();
  MDNS.enableArduino(3232, true); // lists it as an updatable board, e.g. in the Arduino IDE
  Serial.printf("Updates over Wi-Fi: on, at %s.local\n", WiFiStationModule::HOSTNAME);
  return true;
#else
  Serial.println("Updates over Wi-Fi: off (no ota_password in secrets.ini)");
  return false;
#endif
}

void OtaUpdates::poll()
{
#ifdef OTA_PASSWORD
  ArduinoOTA.handle();
#endif
}

OtaUpdates::Kind OtaUpdates::updating() const
{
  return kind;
}

uint8_t OtaUpdates::percent() const
{
  return done;
}
