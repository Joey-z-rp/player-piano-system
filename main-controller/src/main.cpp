#include <Arduino.h>
#include <LittleFS.h>
#include <string>
#include "app/library/piece_library.h"
#include "app/piano/piano_controller.h"
#include "app/piano/velocity_table.h"
#include "led_control.h"
#include "rs485.h"
#include "serial_commands.h"
#include "wifi_station.h"
#include "web/web_server.h"

LedControl ledControl;
Rs485Module rs485;
VelocityTable velocityTable;
PianoController piano;
PieceLibrary library;
SerialCommands serialCommands;
WiFiStationModule wifiStation;
WebServerModule webServer;

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
  Serial.println("Starting ESP32 Piano Controller...");

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
  serialCommands.begin(&piano);
  library.begin();

  Serial.println("Initializing WiFi Station...");
  if (!wifiStation.init())
  {
    Serial.println("WiFi failed. Set credentials in wifi_station.cpp");
    return;
  }

  // The clock, for when pieces were added and played (UTC; the page shows local time).
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  if (!webServer.begin(piano, library))
  {
    Serial.println("Failed to start web server");
    return;
  }
  ledControl.set(CRGB::Green);
  Serial.printf("Open http://%s/\n", wifiStation.getIPAddress().toString().c_str());
}

// Playback and RS485 run on the piano controller's own task; the loop only
// reads commands from the USB serial port.
void loop()
{
  serialCommands.poll();
  delay(1);
}
