#include "app/piano/live_player.h"
#include "app/piano/roll_builder.h"
#include "app/piano/velocity_table.h"
#include <algorithm>

namespace
{
  // The two clocks are followed by the quickest message in each window, so
  // a drift between them, or a slower route, is caught up with; after a
  // silence this long the next message starts afresh.
  const uint32_t CLOCK_WINDOW_MS = 10000;
  const uint32_t RESYNC_AFTER_MS = 5000;
  // Pedal changes kept to tell whether a note plays with the pedal down.
  const int64_t PEDAL_MEMORY_MS = 10000;
  const uint8_t LOWEST_NOTE = 21; // A0
} // namespace

void LivePlayer::start(uint16_t delay, const PlayConfig &rules, const VelocityTable *velocities)
{
  running = true;
  delayMs = std::min(delay, MAX_LIVE_DELAY_MS);
  config = rules;
  table = velocities;
  synced = false;
  windowHasSample = false;
  pending.clear();
  std::fill(keys, keys + PIANO_KEY_COUNT, Key());
  pedalChanges.clear();
  pedalDown = false;
  lastPedalMs = INT64_MIN / 2;
  lateCount = 0;
}

void LivePlayer::stop()
{
  running = false;
  pending.clear();
}

void LivePlayer::setDelay(uint16_t delay)
{
  delayMs = std::min(delay, MAX_LIVE_DELAY_MS);
}

int64_t LivePlayer::toLocal(uint32_t senderMs, uint32_t nowMs)
{
  int64_t seen = (int64_t)nowMs - senderMs;
  if (!synced || nowMs - lastMessageMs > RESYNC_AFTER_MS)
  {
    synced = true;
    offsetMs = seen;
    windowMinMs = seen;
    windowStartMs = nowMs;
    windowHasSample = true;
  }
  else
  {
    offsetMs = std::min(offsetMs, seen); // a quicker one: use it at once
    windowMinMs = windowHasSample ? std::min(windowMinMs, seen) : seen;
    windowHasSample = true;
    if (nowMs - windowStartMs >= CLOCK_WINDOW_MS)
    {
      offsetMs = windowMinMs; // may be later, if the clocks have drifted
      windowStartMs = nowMs;
      windowHasSample = false;
    }
  }
  lastMessageMs = nowMs;
  return (int64_t)senderMs + offsetMs + delayMs;
}

void LivePlayer::clock(uint32_t senderMs, uint32_t nowMs)
{
  if (running)
  {
    toLocal(senderMs, nowMs);
  }
}

void LivePlayer::schedule(const LiveAction &action)
{
  auto at = std::upper_bound(pending.begin(), pending.end(), action.atMs,
                             [](uint32_t ms, const LiveAction &a) { return (int32_t)(ms - a.atMs) < 0; });
  pending.insert(at, action);
}

LiveAction *LivePlayer::pendingRelease(uint8_t key)
{
  for (LiveAction &action : pending)
  {
    if (action.type == LiveAction::Type::Release && action.key == key)
    {
      return &action;
    }
  }
  return nullptr;
}

// As the roll builder's markPedal: down pedalSettleMs before the earliest the
// key could be pressed, and not lifted before it sounds (as far as is known).
bool LivePlayer::pedalHeldFor(int64_t pressMs, int64_t soundsMs) const
{
  int64_t settled = pressMs - config.pedalSettleMs;
  bool down = false;
  for (const PedalChange &change : pedalChanges)
  {
    if (change.atMs <= settled)
    {
      down = change.down;
    }
    else if (change.atMs <= soundsMs && !change.down)
    {
      return false;
    }
  }
  return down;
}

void LivePlayer::message(uint32_t senderMs, uint32_t nowMs, uint8_t status, uint8_t data1, uint8_t data2)
{
  if (!running)
  {
    return;
  }
  uint8_t kind = status & 0xF0;
  bool on = kind == 0x90 && data2 > 0, off = kind == 0x80 || (kind == 0x90 && data2 == 0);
  bool sustain = kind == 0xB0 && data1 == 64;
  if (!on && !off && !sustain)
  {
    return;
  }
  int64_t atMs = toLocal(senderMs, nowMs);
  if (sustain)
  {
    pedal(data2 >= 64, atMs, nowMs);
    return;
  }
  if (data1 < LOWEST_NOTE || data1 >= LOWEST_NOTE + PIANO_KEY_COUNT)
  {
    return;
  }
  uint8_t key = data1 - LOWEST_NOTE;
  if (on)
  {
    noteOn(key, std::min<uint8_t>(data2, 127), atMs, nowMs);
  }
  else
  {
    noteOff(key, atMs);
  }
}

void LivePlayer::noteOn(uint8_t key, uint8_t velocity, int64_t soundsMs, uint32_t nowMs)
{
  Key &k = keys[key];
  if (k.held)
  {
    // Still down (no note-off yet): it ends where this one starts, and that
    // note's own off is for a note no longer playing.
    scheduleRelease(key, soundsMs);
    k.ignoreOffs++;
  }
  int64_t landingUp = table != nullptr ? table->landingMs(key, velocity, false) : 0;
  bool pedal = pedalHeldFor(soundsMs - landingUp, soundsMs);
  uint16_t landing = table != nullptr ? table->landingMs(key, velocity, pedal) : 0;
  uint16_t duty = table != nullptr ? table->duty(key, velocity, pedal) : VelocityTable::defaultDuty(velocity);
  int64_t pressMs = soundsMs - landing;
  bool repeat = false;
  if (k.struck)
  {
    int32_t interval = (int32_t)(soundsMs - k.soundsMs);
    repeat = interval < (int32_t)config.repeatWindowMs;
    int64_t earliestRelease = k.soundsMs + config.minNoteMs;
    LiveAction *release = k.releasePending ? pendingRelease(key) : nullptr;
    if (!canRepeatAt(config, k.velocity, velocity, interval) || pressMs <= earliestRelease)
    {
      if (release != nullptr)
      {
        // Too soon to strike again: held in the strike before, until this
        // note ends.
        pending.erase(pending.begin() + (release - pending.data()));
        k.releasePending = false;
        k.held = true;
        return;
      }
      // Already let go: struck as soon as it can be.
      pressMs = std::max(pressMs, k.releaseMs + config.restrikeGapMs);
    }
    else if (release != nullptr)
    {
      // Released in time to strike again.
      int64_t releaseMs = std::max(earliestRelease, std::min<int64_t>(release->atMs, pressMs - config.restrikeGapMs));
      LiveAction moved = *release;
      pending.erase(pending.begin() + (release - pending.data()));
      moved.atMs = (uint32_t)std::max<int64_t>(releaseMs, nowMs);
      k.releaseMs = moved.atMs;
      schedule(moved);
    }
    else
    {
      pressMs = std::max(pressMs, k.releaseMs + config.restrikeGapMs);
    }
  }
  if (pressMs < (int64_t)nowMs)
  {
    lateCount++;
    pressMs = nowMs;
  }
  schedule({(uint32_t)pressMs, LiveAction::Type::Press, key, pedal, repeat, duty, landing});
  k.struck = true;
  k.held = true;
  k.velocity = velocity;
  k.soundsMs = pressMs + landing;
}

void LivePlayer::noteOff(uint8_t key, int64_t atMs)
{
  Key &k = keys[key];
  if (k.ignoreOffs > 0)
  {
    k.ignoreOffs--;
    return;
  }
  if (k.held)
  {
    scheduleRelease(key, atMs);
  }
}

void LivePlayer::scheduleRelease(uint8_t key, int64_t atMs)
{
  Key &k = keys[key];
  // Held at least until its hammer has landed.
  int64_t releaseMs = std::max<int64_t>(atMs, k.soundsMs + config.minNoteMs);
  k.held = false;
  k.releasePending = true;
  k.releaseMs = releaseMs;
  schedule({(uint32_t)releaseMs, LiveAction::Type::Release, key, false, false, 0, 0});
}

void LivePlayer::pedal(bool down, int64_t atMs, uint32_t nowMs)
{
  if (down == pedalDown)
  {
    return;
  }
  pedalDown = down;
  atMs = std::max(atMs, lastPedalMs + config.pedalMinMs); // its solenoids need the time
  if (atMs < (int64_t)nowMs)
  {
    lateCount++;
    atMs = nowMs;
  }
  lastPedalMs = atMs;
  // Keep the changes that can still matter (and the last before them).
  while (pedalChanges.size() > 1 && pedalChanges[1].atMs < (int64_t)nowMs - PEDAL_MEMORY_MS)
  {
    pedalChanges.erase(pedalChanges.begin());
  }
  pedalChanges.push_back({atMs, down});
  schedule({(uint32_t)atMs, down ? LiveAction::Type::PedalDown : LiveAction::Type::PedalUp, 0, false, false, 0, 0});
}

void LivePlayer::takeDue(uint32_t nowMs, std::vector<LiveAction> &out)
{
  size_t due = 0;
  while (due < pending.size() && (int32_t)(nowMs - pending[due].atMs) >= 0)
  {
    const LiveAction &action = pending[due];
    if (action.type == LiveAction::Type::Release)
    {
      keys[action.key].releasePending = false;
    }
    out.push_back(action);
    due++;
  }
  pending.erase(pending.begin(), pending.begin() + due);
}

bool LivePlayer::nextDue(uint32_t &atMs) const
{
  if (pending.empty())
  {
    return false;
  }
  atMs = pending.front().atMs;
  return true;
}
