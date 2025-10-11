#include "led_control.h"

LedControl::LedControl() : led(CRGB::Black)
{
}

LedControl::~LedControl()
{
  // Cleanup if needed
}

bool LedControl::init()
{
  // Initialize FastLED with hardcoded pin 48 and 1 LED
  FastLED.addLeds<WS2812B, 48, GRB>(&led, 1);
  FastLED.setBrightness(50); // Default brightness

  // Initialize LED to off
  led = CRGB::Black;
  FastLED.show();

  return true;
}

void LedControl::set(CRGB color, uint8_t brightness)
{
  // Apply brightness to color
  color.nscale8(brightness);

  // Set LED color
  led = color;

  // Update display
  FastLED.show();
}