#include <Arduino.h>
#include <FastLED.h>

// RGB LED on GPIO 48
#define LED_PIN 48
#define NUM_LEDS 1

// Create LED array
CRGB leds[NUM_LEDS];

void setup()
{
  // Initialize serial communication for debugging
  Serial.begin(9600);

  // Initialize FastLED with GPIO 48
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50); // Set brightness (0-255)

  // Print hello world message
  Serial.println("Hello World! ESP32-S3 RGB LED on GPIO 48");
  Serial.println("RGB LED will cycle through colors");

  // Clear LED initially
  leds[0] = CRGB::Black;
  FastLED.show();
}

void loop()
{
  // Red
  Serial.println("Red");
  leds[0] = CRGB::Red;
  FastLED.show();
  delay(2000);

  // Green
  Serial.println("Green");
  leds[0] = CRGB::Green;
  FastLED.show();
  delay(2000);

  // Blue
  Serial.println("Blue");
  leds[0] = CRGB::Blue;
  FastLED.show();
  delay(2000);

  // Off
  Serial.println("Off");
  leds[0] = CRGB::Black;
  FastLED.show();
  delay(2000);
}