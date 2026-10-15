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

// Longest landing time accepted from a table, in ms.
static const uint16_t MAX_LANDING_MS = 1000;

// Most steps a key's stepped drive setting can have (see KeySetting).
static const uint8_t MAX_SETTING_STEPS = 6;

// How a key press is driven once the table has landing times (see
// pressCommand). These are the defaults; the table's "drive" replaces them
// (from tools/calibrate_dynamics.py's settings). Duties in hundredths of a
// percent, times in ms.
struct DriveSettings
{
  uint16_t strikeMs = 50;
  uint16_t coastDuty = 2000;   // about what carries the key's weight, so the hammer coasts on
  int16_t coastOffsetMs = 10;  // without a clamp: when the hold starts, from the hammer landing
  int16_t clampOffsetMs = -15; // with a clamp: when it starts, from the hammer landing
  uint16_t clampMs = 0;        // 0 = no clamp
  uint16_t clampDuty = 6000;
  uint16_t holdDuty = 2500;
};

// The drive settings a key can set for itself, each one value or steps by
// strike duty.
enum class KeySetting : uint8_t
{
  CoastOffsetMs,
  ClampOffsetMs,
  ClampMs,
  ClampDuty,
  HoldDuty,
  Count
};

// Keys the calibration doesn't cover map velocity 1-127 linearly onto this
// duty range.
static const uint8_t DEFAULT_MIN_DUTY = 35;
static const uint8_t DEFAULT_MAX_DUTY = 80;

// The duty to strike each key with at each velocity, from the loudness
// calibration: tools/calibrate_dynamics.py table writes it to
// data/velocity_table.json, uploaded to the controller with uploadfs. The
// table can also say when the hammer lands after the press at each velocity
// (per key where the calibration measured it, and a shared curve for the
// rest), how presses are driven ("drive"), and a key's own drive settings.
class VelocityTable
{
public:
  VelocityTable();

  // Reads the table's JSON: each key's "duty" list, in percent with up to two
  // decimals, its optional "landing_ms" list and own drive settings ("coast",
  // "coast_offset_ms", "clamp_offset_ms", "clamp_ms", "clamp_duty", "hold"),
  // and the optional shared "landing_ms" and "drive". All or nothing: on an
  // error the table is unchanged and error says why.
  bool load(const char *json, size_t length, std::string &error);

  // In hundredths of a percent.
  uint16_t duty(uint8_t key, uint8_t velocity) const;
  bool isCalibrated(uint8_t key) const;
  size_t calibratedKeys() const;

  // Whether the table has any landing times.
  bool hasLanding() const;
  // Ms from the press until the key's hammer lands at this velocity: the
  // key's own, else the shared curve's, else 0 (unknown).
  uint16_t landingMs(uint8_t key, uint8_t velocity) const;
  // The latest landing of any key at any velocity.
  uint16_t maxLandingMs() const;

  // How presses are driven: the table's "drive", or the defaults.
  const DriveSettings &drive() const;
  // A key's coast duty: its own ("coast"), else the drive's.
  uint16_t coastDuty(uint8_t key) const;
  // A key's setting for a strike at strikeDuty: its own where the table sets
  // it, else the drive's. Its own is one value, or steps of [strike duty
  // above which, value], e.g. "hold": [[0,35],[45,40]]. Duties in hundredths.
  int32_t setting(uint8_t key, KeySetting which, uint16_t strikeDuty) const;
  uint16_t holdDuty(uint8_t key, uint16_t strikeDuty) const;

  // The driver command that presses this key at this velocity. The strike
  // duty for strikeMs; then the coast duty, carrying the hammer through its
  // flight; then, without a clamp, the hold from coastOffsetMs after the
  // hammer lands (negative = before), e.g. "P:40:33.25:50:20:93:25"; or with
  // one, the clamp duty for clampMs from clampOffsetMs after the hammer lands,
  // pinning the key as it bottoms out so it can't rebound into a second
  // strike, then the hold, e.g. "P:30:65:50:20:6:60:40:35". Nothing starts
  // before the strike ends. Without landing times, a plain "P:key:duty".
  // tools/calibration_common.py's drive_command does the same.
  void pressCommand(uint8_t key, uint8_t velocity, char *out, size_t size) const;

  // Percent with up to two decimals, as the drivers take it: 3225 -> "32.25".
  static void formatDuty(char *text, size_t size, uint16_t duty);

  static uint16_t defaultDuty(uint8_t velocity);

private:
  uint16_t duties[PIANO_KEY_COUNT][VELOCITY_LEVELS];
  bool calibrated[PIANO_KEY_COUNT];
  uint16_t keyLanding[PIANO_KEY_COUNT][VELOCITY_LEVELS];
  bool keyHasLanding[PIANO_KEY_COUNT];
  uint16_t sharedLanding[VELOCITY_LEVELS];
  bool hasSharedLanding;
  DriveSettings driveSettings;
  uint16_t keyCoast[PIANO_KEY_COUNT]; // 0 = the drive's
  struct Steps
  {
    uint16_t above[MAX_SETTING_STEPS]; // strike duty
    int32_t value[MAX_SETTING_STEPS];
    uint8_t count; // 0 = the drive's
  };
  Steps keySettings[PIANO_KEY_COUNT][(size_t)KeySetting::Count];
};

#endif // VELOCITY_TABLE_H
