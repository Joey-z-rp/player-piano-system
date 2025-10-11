#include <Arduino.h>
#include <FastLED.h>
#include "ble_midi.h"

// RGB LED on GPIO 48
#define LED_PIN 48
#define NUM_LEDS 1

// Create LED array
CRGB leds[NUM_LEDS];

// Create BLE MIDI module instance
BleMidiModule bleMidi;

void setup()
{
  // Initialize serial communication for debugging
  Serial.begin(115200);

  // Initialize FastLED with GPIO 48
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50); // Set brightness (0-255)

  // Print hello world message
  Serial.println("Hello World! ESP32-S3 RGB LED on GPIO 48");
  Serial.println("Initializing BLE MIDI device...");

  // Initialize BLE MIDI module
  if (!bleMidi.begin("ESP32-S3 Piano", leds, NUM_LEDS))
  {
    Serial.println("Failed to initialize BLE MIDI module!");
    return;
  }

  // Clear LED initially
  leds[0] = CRGB::Black;
  FastLED.show();
}

void loop()
{

  // Small delay to prevent overwhelming the system
  delay(10);
}