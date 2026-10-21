#ifndef KEY_OFFSETS_H
#define KEY_OFFSETS_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include "app/piano/piano_roll.h"

// Touch-ups to the velocity table, made by ear on the test page: a key that
// plays too loud or soft (at some velocities, or all) is played as if it had
// been given another velocity, so it stays on its own measured curve, landing
// time included. Saved on the controller; the table is left as built.
static const uint8_t MAX_OFFSET_POINTS = 8;
static const uint8_t TOP_VELOCITY = 127;
static const int8_t MAX_VELOCITY_OFFSET = 60;

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

// Every key's curve with the pedal up, and with it down. A key without a
// pedal-down curve uses its pedal-up one either way.
struct KeyOffsets
{
  OffsetCurve up[PIANO_KEY_COUNT];
  OffsetCurve pedal[PIANO_KEY_COUNT];
  bool hasPedal[PIANO_KEY_COUNT] = {};

  uint8_t adjust(uint8_t key, uint8_t velocity, bool pedalDown) const;
  bool empty() const;
};

// Reads [[velocity, offset], ...] (empty for none). Points must rise in
// velocity, with offsets within MAX_VELOCITY_OFFSET, and the curve must never
// play a higher velocity softer than a lower one (fall faster than one
// velocity per velocity). On an error curve is unchanged and error says why.
bool readOffsetCurve(const char *json, size_t length, OffsetCurve &curve, std::string &error);

// The whole set: {"keys": {"44": {"up": [[64, -4]], "pedal": [[64, -2]]}, ...}}
// ("pedal" only where a key has its own). All or nothing, as above.
bool readKeyOffsets(const char *json, size_t length, KeyOffsets &offsets, std::string &error);
std::string keyOffsetsJson(const KeyOffsets &offsets);
std::string offsetCurveJson(const OffsetCurve &curve);

#endif // KEY_OFFSETS_H
