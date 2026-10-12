#ifndef VELOCITY_TABLE_H
#define VELOCITY_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include "app/piano/piano_roll.h"

static const uint8_t VELOCITY_LEVELS = 127; // MIDI velocities 1-127

// Duties are in hundredths of a percent (10000 = 100%), as the drivers take
// them; the drivers' PWM realises them in 0.25% steps.
static const uint16_t DUTY_FULL = 10000;

// Keys the calibration doesn't cover map velocity 1-127 linearly onto this
// duty range.
static const uint8_t DEFAULT_MIN_DUTY = 35;
static const uint8_t DEFAULT_MAX_DUTY = 80;

// The duty to strike each key with at each velocity, from the loudness
// calibration: tools/calibrate_dynamics.py table writes it to
// data/velocity_table.json, uploaded to the controller with uploadfs.
class VelocityTable
{
public:
  VelocityTable();

  // Reads the table's JSON (only each key's "duty" list is used, in percent
  // with up to two decimals). All or nothing: on an error the table is
  // unchanged and error says why.
  bool load(const char *json, size_t length, std::string &error);

  // In hundredths of a percent.
  uint16_t duty(uint8_t key, uint8_t velocity) const;
  bool isCalibrated(uint8_t key) const;
  size_t calibratedKeys() const;

  static uint16_t defaultDuty(uint8_t velocity);

private:
  uint16_t duties[PIANO_KEY_COUNT][VELOCITY_LEVELS];
  bool calibrated[PIANO_KEY_COUNT];
};

#endif // VELOCITY_TABLE_H
