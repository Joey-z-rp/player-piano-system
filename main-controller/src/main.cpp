#include <Arduino.h>
#include "led_control.h"
#include "wifi_station.h"
#include "web_server.h"

LedControl ledControl;
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

  Serial.println("Initializing WiFi Station...");
  if (!wifiStation.init())
  {
    Serial.println("WiFi failed. Set credentials in wifi_station.cpp");
    return;
  }

  ledControl.set(CRGB::Green);
  webServer.begin();
  Serial.printf("Open http://%s/\n", wifiStation.getIPAddress().toString().c_str());
}

void loop()
{
  if (wifiStation.isConnected())
  {
    webServer.handle();
  }
}
