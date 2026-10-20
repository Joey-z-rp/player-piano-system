#ifndef PLAY_CONFIG_H
#define PLAY_CONFIG_H

#include <stddef.h>
#include <stdint.h>
#include <string>

// How long a repeat (a key struck again soon after its last note) needs, by
// how loud it is: see PlayConfig.
struct RepeatLimit
{
  uint8_t from;         // the repeat's velocity, from this up (to the next limit's)
  uint16_t afterSoftMs; // after a note softer than PlayConfig::repeatLoudFrom
  uint16_t afterLoudMs; // after a louder one
};

// A clamp's timing: from offsetMs after the hammer lands (negative = before),
// for ms, at duty (hundredths of a percent). 0 ms: none.
struct ClampSettings
{
  int16_t offsetMs;
  uint16_t ms;
  uint16_t duty;
};

static const uint8_t MAX_REPEAT_LIMITS = 8;
static const size_t PEDAL_COMMAND_LENGTH = 24;

// The timing rules pieces are played with: how long notes are held, how
// soon a key can strike again, and the pedal. Chosen by ear on the config
// page and saved on the controller (the velocity table holds what the
// calibration measured). These defaults are what was measured on this piano
// (tools/repeat_tests.py, an upright).
struct PlayConfig
{
  // Notes.
  uint16_t minNoteMs = 10;     // shortest a key is held after its hammer lands
  uint16_t restrikeGapMs = 70; // a key is released this long before it strikes again, when there's time

  // Repeats: a repeat closer than its limit (between the notes sounding) is
  // merged into the note before. Limits are in falling order of "from", the
  // last from 0.
  RepeatLimit repeatLimits[MAX_REPEAT_LIMITS] = {
      {100, 90, 120}, {76, 90, 135}, {56, 150, 150}, {40, 165, 165}, {0, 195, 180},
  };
  uint8_t repeatLimitCount = 5;
  uint8_t repeatLoudFrom = 96; // a note before from this velocity up counts as loud
  // Repeats in this velocity range, after a softer note, also miss in this
  // interval (the action is still moving), though they play either side of it.
  uint8_t deadZoneFromVelocity = 76;
  uint8_t deadZoneToVelocity = 99;
  uint16_t deadZoneFromMs = 105;
  uint16_t deadZoneToMs = 120;
  // A strike this soon after the last on its key is a repeat: sent repeatLateMs
  // early (its hammer lands about that much later) and clamped with
  // repeatClamp in place of the key's own clamp (else it can bounce).
  uint16_t repeatWindowMs = 250;
  uint16_t repeatLateMs = 10;
  ClampSettings repeatClamp = {-20, 100, 10000};

  // The sustain pedal.
  uint16_t pedalSettleMs = 100; // how long it takes to lift the dampers after it's sent down
  uint16_t pedalMinMs = 100;    // shortest time between its changes
  // Its press and release commands (after "P:<channel>:"), sent to both its solenoids.
  char pedalPress[PEDAL_COMMAND_LENGTH] = "100:30:100:30:55";
  char pedalRelease[PEDAL_COMMAND_LENGTH] = "40:40:30:40:0";
};

// Reads the settings in json over config: any not given keep their values.
// All or nothing: on an error config is unchanged and error says why.
bool readPlayConfig(const char *json, size_t length, PlayConfig &config, std::string &error);

// The settings as JSON, in the form readPlayConfig takes.
std::string playConfigJson(const PlayConfig &config);

bool operator==(const PlayConfig &a, const PlayConfig &b);
inline bool operator!=(const PlayConfig &a, const PlayConfig &b)
{
  return !(a == b);
}

#endif // PLAY_CONFIG_H
