#include <Arduino.h>
#include "app/piano_controller.h"
#include "led_control.h"
#include "rs485.h"
#include "wifi_station.h"
#include "web/web_server.h"

LedControl ledControl;
Rs485Module rs485;
PianoController piano;
WiFiStationModule wifiStation;
WebServerModule webServer;

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

  if (!piano.init(&rs485))
  {
    Serial.println("Failed to initialize piano controller");
    return;
  }

  Serial.println("Initializing WiFi Station...");
  if (!wifiStation.init())
  {
    Serial.println("WiFi failed. Set credentials in wifi_station.cpp");
    return;
  }

  if (!webServer.begin(piano))
  {
    Serial.println("Failed to start web server");
    return;
  }
  ledControl.set(CRGB::Green);
  Serial.printf("Open http://%s/\n", wifiStation.getIPAddress().toString().c_str());
}

void loop()
{
  piano.process();
}
