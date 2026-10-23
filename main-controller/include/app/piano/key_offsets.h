#ifndef KEY_OFFSETS_H
#define KEY_OFFSETS_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include "app/piano/piano_roll.h"

// Touch-ups to the velocity table, made by ear on the Keys page, saved on
// the controller (the table is left as built). Two kinds, per key:
// - velocity offsets: a key that plays too loud or soft (at some velocities,
//   or all) is played as if it had been given another velocity, so it stays
//   on its own measured curve, landing time included;
// - duty changes: a duty added to (or taken from) what the table gives, for
//   a key the table can't fix, e.g. one whose calibration stopped short of
//   its loudest. Its landing time then follows the duty it's struck with
//   (VelocityTable::landingMs).
static const uint8_t MAX_OFFSET_POINTS = 8;
static const uint8_t TOP_VELOCITY = 127;
static const int8_t MAX_VELOCITY_OFFSET = 60;
static const int16_t MAX_DUTY_CHANGE = 5000; // hundredths of a percent: +/-50%
static const int16_t DUTY_CHANGE_STEP = 25;  // the drivers' 0.25% PWM steps

struct OffsetPoint
{
  uint8_t velocity; // 1-127
  int8_t offset;    // velocities added
};

// An offset by velocity: straight lines between the points (in rising order
// of velocity), flat beyond the first and last. No points: no offset.
struct OffsetCurve
{
  OffsetPoint points[MAX_OFFSET_POINTS] = {};
  uint8_t count = 0;

  // The velocity played for this one: velocity + its offset (rounded half
  // away from zero), kept within 1-127. tools/calibration_common.py's
  // adjusted_velocity does the same.
  uint8_t adjust(uint8_t velocity) const;
  bool operator==(const OffsetCurve &other) const;
};

struct DutyPoint
{
  uint8_t velocity; // 1-127
  int16_t change;   // hundredths of a percent
};

// A duty change by velocity (the velocity a piece asks for, before any
// offset): straight lines between the points (in rising order of velocity),
// flat beyond the first and last. No points: no change.
struct DutyCurve
{
  DutyPoint points[MAX_OFFSET_POINTS] = {};
  uint8_t count = 0;

  // In hundredths of a percent, rounded to DUTY_CHANGE_STEP (half away from
  // zero). tools/calibration_common.py's duty_change does the same.
  int16_t change(uint8_t velocity) const;
  bool operator==(const DutyCurve &other) const;
};

// Every key's curves with the pedal up, and with it down. A key without a
// pedal-down curve (of either kind) uses its pedal-up one either way.
struct KeyOffsets
{
  OffsetCurve up[PIANO_KEY_COUNT];
  OffsetCurve pedal[PIANO_KEY_COUNT];
  bool hasPedal[PIANO_KEY_COUNT] = {};
  DutyCurve dutyUp[PIANO_KEY_COUNT];
  DutyCurve dutyPedal[PIANO_KEY_COUNT];
  bool hasDutyPedal[PIANO_KEY_COUNT] = {};

  uint8_t adjust(uint8_t key, uint8_t velocity, bool pedalDown) const;
  int16_t dutyChange(uint8_t key, uint8_t velocity, bool pedalDown) const;
  // Whether the key has anything set.
  bool has(uint8_t key) const;
  bool empty() const;
  // One key's curves, as readKeyEntry reads them.
  struct Entry
  {
    OffsetCurve up, pedal;
    bool hasPedal = false;
    DutyCurve dutyUp, dutyPedal;
    bool hasDutyPedal = false;
  };
  Entry entry(uint8_t key) const;
  void set(uint8_t key, const Entry &entry);
};

// Reads [[velocity, offset], ...] (empty for none). Points must rise in
// velocity, with offsets within MAX_VELOCITY_OFFSET, and the curve must never
// play a higher velocity softer than a lower one (fall faster than one
// velocity per velocity). On an error curve is unchanged and error says why.
bool readOffsetCurve(const char *json, size_t length, OffsetCurve &curve, std::string &error);

// One key's curves, as keyOffsetsJson gives each: {"up": [[64, -4]],
// "pedal": [[64, -2]], "duty": [[64, 0], [127, 23]], "duty_pedal": [...]},
// any left out (or null): none, and for "pedal" and "duty_pedal" the pedal-up
// one is used with the pedal down too. Duty changes are in percent, in 0.25%
// steps, within +/-50%. Points rise in velocity; the duty curve can fall
// freely (whether the duties played still rise depends on the table, which
// the Keys page checks). On an error entry is unchanged and error says why.
bool readKeyEntry(const char *json, size_t length, KeyOffsets::Entry &entry, std::string &error);

// The whole set: {"keys": {"44": {entry as above}, ...}} (only keys with
// something set). All or nothing, as above.
bool readKeyOffsets(const char *json, size_t length, KeyOffsets &offsets, std::string &error);
std::string keyOffsetsJson(const KeyOffsets &offsets);
std::string offsetCurveJson(const OffsetCurve &curve);

#endif // KEY_OFFSETS_H
