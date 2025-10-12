#include <Arduino.h>
#include "ble_midi.h"
#include "led_control.h"
#include "wifi_station.h"

// Create module instances
BleMidiModule bleMidi;
LedControl ledControl;
WiFiStationModule wifiStation;

void setup()
{
  Serial.begin(115200);
  Serial.println("Starting ESP32 Piano Controller...");

  // Initialize LED control first
  Serial.println("Initializing LED control...");
  if (!ledControl.init())
  {
    Serial.println("Failed to initialize LED control module!");
    return;
  }

  // Initialize WiFi Station
  Serial.println("Initializing WiFi Station...");
  if (!wifiStation.init())
  {
    Serial.println("Failed to connect to WiFi!");
    Serial.println("Please update WiFi credentials in wifi_station.h");
    // Continue without WiFi - BLE will still work
  }

  // Add delay before BLE initialization
  delay(1000);
  Serial.println("Initializing BLE MIDI...");

  // Initialize BLE MIDI module with LED control
  if (!bleMidi.init("ESP32-S3 Piano", &ledControl))
  {
    Serial.println("Failed to initialize BLE MIDI module!");
    return;
  }

  // Set initial LED state
  ledControl.set(CRGB::Black);

  Serial.println("ESP32 Piano Controller initialized successfully!");

  // Display connection status
  if (wifiStation.isConnected())
  {
    Serial.printf("WiFi: Connected to %s (IP: %s)\n",
                  wifiStation.getSSID().c_str(),
                  wifiStation.getIPAddress().toString().c_str());
    Serial.printf("Signal Strength: %d dBm\n", wifiStation.getRSSI());
  }
  else
  {
    Serial.println("WiFi: Not connected");
  }

  Serial.println("BLE MIDI: Ready for connections");
}

void loop()
{
  // Monitor WiFi connection status every 30 seconds
  static unsigned long lastStatusCheck = 0;
  if (millis() - lastStatusCheck > 30000)
  {
    lastStatusCheck = millis();

    if (wifiStation.isConnected())
    {
      Serial.printf("WiFi: Connected to %s (RSSI: %d dBm)\n",
                    wifiStation.getSSID().c_str(),
                    wifiStation.getRSSI());
    }
    else
    {
      Serial.println("WiFi: Disconnected - attempting reconnection...");
      wifiStation.init(); // Try to reconnect
    }
  }

  // Small delay to prevent overwhelming the system
  delay(100);
}