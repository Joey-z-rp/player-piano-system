#ifndef HEAT_MODEL_H
#define HEAT_MODEL_H

#include <stdint.h>

// Channels the drivers have: keys 0-87, then the sustain pedal's two
// solenoids (88 and 89).
static const uint8_t HEAT_CHANNELS = 90;
static const uint8_t FIRST_PEDAL_CHANNEL = 88;

// One kind of solenoid's coil (the keys', or the pedal's): measured, it
// can be watched; any of them 0 means not measured, and not watched.
struct CoilSettings
{
  float ohms = 0;     // its resistance, cold
  float rthCPerW = 0; // its thermal resistance: °C of rise per watt, held
  float tauS = 0;     // how quickly it gets there: the time constant, in seconds

  bool measured() const
  {
    return ohms > 0 && rthCPerW > 0 && tauS > 0;
  }
};

struct HeatSettings
{
  float volts = 24;
  // At the drivers' 20 kHz the coil current is about steady, at duty x V / R
  // with a flyback diode, so the heat goes with duty squared; with a
  // fast-decay clamp it's closer to duty itself.
  bool squared = true;
  CoilSettings keys, pedal;
  float limitC = 50; // °C of rise allowed
};

// Estimates every solenoid's temperature rise from the commands sent to the
// drivers: each command's duties over time (as the drivers play them: the
// strike, its phases, then the hold until released) give the coil's power,
// V^2/R x duty^2 (or duty), and the coil warms towards power x Rth with time
// constant tau, and cools towards 0 the same way.
class HeatModel
{
public:
  HeatModel();

  void configure(const HeatSettings &settings);
  const HeatSettings &settings() const;

  // A command as sent to the drivers ("P:...", "R:...", "RESET:ALL"),
  // sent at nowMs.
  void command(const char *command, uint32_t nowMs);
  // Brings every channel's estimate up to nowMs.
  void update(uint32_t nowMs);

  bool watched(uint8_t channel) const; // its coil's been measured
  // Whether its driver has it on at nowMs (as the commands sent say).
  bool on(uint8_t channel, uint32_t nowMs) const;
  float riseC(uint8_t channel) const;
  // How close it is to the limit: its rise over limitC (1 = at it).
  float load(uint8_t channel) const;
  // The watched channel nearest its limit, or HEAT_CHANNELS if none is watched.
  uint8_t hottest() const;
  // Seconds until it cools to `load` (of the limit), with nothing more sent.
  float secondsToCool(uint8_t channel, float load) const;

private:
  // What a channel's driver is doing: from startMs, each segment's duty
  // (fraction) until its end (ms after the start), then the hold.
  struct Drive
  {
    uint32_t startMs = 0;
    uint8_t segments = 0;
    float duty[5] = {};
    uint32_t endMs[5] = {};
    float hold = 0;
  };
  struct Channel
  {
    Drive drive;
    float rise = 0;
    uint32_t updatedMs = 0;
  };

  const CoilSettings &coil(uint8_t channel) const;
  float heating(float duty) const; // the power's share of V^2/R at this duty
  // The mean of heating(duty) over [fromMs, toMs).
  float meanHeating(const Drive &drive, uint32_t fromMs, uint32_t toMs) const;
  void advance(uint8_t channel, uint32_t nowMs);
  void drive(uint8_t channel, const Drive &drive, uint32_t nowMs);

  HeatSettings config;
  Channel channels[HEAT_CHANNELS];
};

#endif // HEAT_MODEL_H
