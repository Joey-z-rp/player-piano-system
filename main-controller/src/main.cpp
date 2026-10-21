#include <Arduino.h>
#include <LittleFS.h>
#include <string>
#include "app/library/piece_library.h"
#include "app/piano/piano_controller.h"
#include "app/piano/velocity_table.h"
#include "firmware_info.h"
#include "led_control.h"
#include "ota_updates.h"
#include "rs485.h"
#include "serial_commands.h"
#include "wifi_station.h"
#include "web/api_system.h"
#include "web/web_server.h"

LedControl ledControl;
Rs485Module rs485;
VelocityTable velocityTable;
PianoController piano;
PieceLibrary library;
SerialCommands serialCommands;
WiFiStationModule wifiStation;
WebServerModule webServer;
OtaUpdates ota;
static bool online = false; // on Wi-Fi with the web server up: the firmware is kept

static const char VELOCITY_TABLE_PATH[] = "/velocity_table.json";

// Without a usable table every key plays the default velocity mapping.
static void loadVelocityTable()
{
  if (!LittleFS.begin(true))
  {
    Serial.println("Velocity table: LittleFS mount failed, using the default mapping");
    return;
  }
  File file = LittleFS.open(VELOCITY_TABLE_PATH, "r");
  if (!file)
  {
    Serial.println("Velocity table: none uploaded, using the default mapping");
    return;
  }
  std::string json(file.size(), '\0');
  size_t read = file.read(reinterpret_cast<uint8_t *>(&json[0]), json.size());
  file.close();
  std::string error = read == json.size() ? "" : "file read cut short";
  if (!error.empty() || !velocityTable.load(json.data(), json.size(), error))
  {
    Serial.printf("Velocity table: %s, using the default mapping\n", error.c_str());
    return;
  }
  Serial.printf("Velocity table: %u keys calibrated (%u with the pedal down too), the rest use the default mapping\n",
                (unsigned)velocityTable.calibratedKeys(), (unsigned)velocityTable.pedalKeys());
}

void setup()
{
  Serial.begin(115200);
  unsigned long serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 5000)
  {
    delay(10);
  }
  Serial.printf("Starting the piano controller: firmware %s, built %s, from %s%s\n", firmwareVersion(),
                firmwareBuilt(), firmwareSlot(),
                firmwareOnTrial() ? " (an update, on trial)" : firmwareRolledBack() ? " (an update was rolled back)"
                                                                                    : "");

  Serial.println("Initializing LED control...");
  if (!ledControl.init())
  {
    Serial.println("Failed to initialize LED control module!");
    return;
  }
  ledControl.set(CRGB::Black);

  Serial.println("Initializing RS485...");
  if (!rs485.init())
  {
    Serial.println("Failed to initialize RS485");
    return;
  }

  loadVelocityTable();
  if (!piano.init(&rs485, &velocityTable))
  {
    Serial.println("Failed to initialize piano controller");
    return;
  }
  serialCommands.begin(&piano, &wifiStation);
  library.begin();

  // Joins the saved network, or starts the setup network (see WiFiStationModule).
  wifiStation.begin();

  // The clock, for when pieces were added and played (UTC; the page shows local time).
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  if (!webServer.begin(piano, library, wifiStation, ota))
  {
    Serial.println("Failed to start web server");
    return;
  }
  ota.begin(piano);
  ledControl.set(wifiStation.inSetup() ? CRGB::Blue : CRGB::Green);
  Serial.printf("Open http://%s/ (or http://%s.local/)\n", wifiStation.getIPAddress().toString().c_str(),
                WiFiStationModule::HOSTNAME);
}

// Playback and RS485 run on the piano controller's own task; the loop reads
// commands from the USB serial port, looks after the Wi-Fi (the setup
// network) and updates over it, and keeps or rolls back an update.
void loop()
{
  serialCommands.poll();
  wifiStation.poll();
  ota.poll();
  restartIfAsked();
  if (!online && wifiStation.isConnected() && !wifiStation.inSetup())
  {
    online = true;
    firmwareIsGood(); // an update that gets this far is kept
    ledControl.set(CRGB::Green);
  }
  checkFirmwareTrial();
  delay(1);
}
