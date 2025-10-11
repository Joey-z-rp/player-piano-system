#include <Arduino.h>
#include "ble_midi.h"
#include "led_control.h"

// Create module instances
BleMidiModule bleMidi;
LedControl ledControl;

void setup()
{
  Serial.begin(115200);
  Serial.println("Initializing LED control...");

  // Initialize LED control module
  if (!ledControl.init())
  {
    Serial.println("Failed to initialize LED control module!");
    return;
  }

  Serial.println("Initializing BLE MIDI device...");

  // Initialize BLE MIDI module with LED control
  if (!bleMidi.init("ESP32-S3 Piano", &ledControl))
  {
    Serial.println("Failed to initialize BLE MIDI module!");
    return;
  }

  // Set initial LED state
  ledControl.set(CRGB::Black);
}

void loop()
{
  // Small delay to prevent overwhelming the system
  delay(10);
}