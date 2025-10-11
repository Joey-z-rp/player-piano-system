#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <Arduino.h>
#include <FastLED.h>

class LedControl
{
public:
  LedControl();
  ~LedControl();

  // Initialize LED control
  bool init();

  // Set LED color and brightness, then update display
  void set(CRGB color, uint8_t brightness = 255);

private:
  CRGB led;
};

#endif // LED_CONTROL_H