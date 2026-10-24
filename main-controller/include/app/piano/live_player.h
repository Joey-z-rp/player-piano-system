#ifndef LIVE_PLAYER_H
#define LIVE_PLAYER_H

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "app/piano/piano_roll.h"
#include "app/piano/play_config.h"

class VelocityTable;

// Live play (the Live page): MIDI from a keyboard somewhere else, played as it
// comes in, a fixed delay after it was played. Each message carries when the
// sender got it (its own clock); the delay is on top of the quickest the
// messages have come across, so the network's ups and downs are smoothed
// out, and it gives time to send each press early by its hammer's landing,
// as a piece's are, so soft and loud notes sound together. Notes go through
// the velocity table (and the keys' adjustments); a repeat too soon for its
// key is held in the strike before, and keys are released in time to strike
// again (the PlayConfig's rules, as for pieces); the pedal is spaced so its
// solenoids keep up. Times here are the controller's millis(). Not safe
// across tasks: the caller locks.
static const uint16_t MIN_LIVE_DELAY_MS = 0;
static const uint16_t MAX_LIVE_DELAY_MS = 2000;
static const uint16_t DEFAULT_LIVE_DELAY_MS = 200;

struct LiveAction
{
  enum class Type : uint8_t
  {
    Press,
    Release,
    PedalDown,
    PedalUp
  };
  uint32_t atMs;
  Type type;
  uint8_t key;    // Press and Release
  bool pedal;     // Press: played as calibrated with the pedal down
  bool repeat;    // Press: a repeat, clamped as one (PlayConfig::repeatClamp)
  uint16_t duty;  // Press: hundredths of a percent
  uint16_t landingMs; // Press: from the press until the hammer lands
};

class LivePlayer
{
public:
  // A new session: nothing pending, every key and the pedal up.
  void start(uint16_t delayMs, const PlayConfig &config, const VelocityTable *table);
  void stop();
  bool active() const
  {
    return running;
  }
  void setDelay(uint16_t delayMs);
  uint16_t delay() const
  {
    return delayMs;
  }

  // A MIDI message (note on, note off, sustain pedal; others are ignored)
  // the sender got at senderMs on its clock, arriving at nowMs.
  void message(uint32_t senderMs, uint32_t nowMs, uint8_t status, uint8_t data1, uint8_t data2);
  // The sender's clock at nowMs (a ping), to follow the two clocks.
  void clock(uint32_t senderMs, uint32_t nowMs);

  // Moves the actions due by nowMs, in order, into out.
  void takeDue(uint32_t nowMs, std::vector<LiveAction> &out);
  // When the next action is due; false if none.
  bool nextDue(uint32_t &atMs) const;
  // Notes and pedal changes that came too late for the delay (played at once).
  uint32_t late() const
  {
    return lateCount;
  }

private:
  int64_t toLocal(uint32_t senderMs, uint32_t nowMs);
  void schedule(const LiveAction &action);
  LiveAction *pendingRelease(uint8_t key);
  bool pedalHeldFor(int64_t pressMs, int64_t soundsMs) const;
  void noteOn(uint8_t key, uint8_t velocity, int64_t soundsMs, uint32_t nowMs);
  void noteOff(uint8_t key, int64_t atMs);
  void scheduleRelease(uint8_t key, int64_t atMs);
  void pedal(bool down, int64_t atMs, uint32_t nowMs);

  struct Key
  {
    bool struck = false;       // pressed at least once this session
    bool held = false;         // pressed, its release not scheduled yet
    bool releasePending = false; // its release scheduled, not sent yet
    uint8_t ignoreOffs = 0;    // note-offs of notes it's no longer playing
    uint8_t velocity = 0;      // its last strike's
    int64_t soundsMs = 0;      // when its last strike sounds
    int64_t releaseMs = 0;     // when its last release is (or was) sent
  };
  struct PedalChange
  {
    int64_t atMs;
    bool down;
  };

  bool running = false;
  uint16_t delayMs = DEFAULT_LIVE_DELAY_MS;
  PlayConfig config;
  const VelocityTable *table = nullptr;
  bool synced = false;
  int64_t offsetMs = 0;    // local minus sender, at the quickest
  int64_t windowMinMs = 0; // the quickest in this window
  uint32_t windowStartMs = 0;
  bool windowHasSample = false;
  uint32_t lastMessageMs = 0;
  std::vector<LiveAction> pending; // by atMs
  Key keys[PIANO_KEY_COUNT];
  std::vector<PedalChange> pedalChanges; // as scheduled, recent ones
  bool pedalDown = false;
  int64_t lastPedalMs = INT64_MIN / 2;
  uint32_t lateCount = 0;
};

#endif // LIVE_PLAYER_H
