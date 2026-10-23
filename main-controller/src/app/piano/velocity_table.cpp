#include "app/piano/velocity_table.h"
#include <algorithm>
#include <math.h>
#include <stdio.h>

static uint8_t clampVelocity(uint8_t velocity)
{
  if (velocity < 1)
  {
    return 1;
  }
  return velocity > VELOCITY_LEVELS ? VELOCITY_LEVELS : velocity;
}

VelocityTable::VelocityTable() : up(), down(), sharedLanding(), hasSharedLanding(false), driveSettings()
{
}

const VelocityTable::Layer &VelocityTable::layer(uint8_t key, bool pedal) const
{
  return pedal && hasPedal(key) ? *down : up;
}

uint16_t VelocityTable::defaultDuty(uint8_t velocity)
{
  velocity = clampVelocity(velocity);
  return DEFAULT_MIN_DUTY * 100 + (uint32_t)(velocity - 1) * (DEFAULT_MAX_DUTY - DEFAULT_MIN_DUTY) * 100 / (VELOCITY_LEVELS - 1);
}

void VelocityTable::setOffsets(std::shared_ptr<const KeyOffsets> offsets)
{
  std::atomic_store(&keyOffsets, std::move(offsets));
}

std::shared_ptr<const KeyOffsets> VelocityTable::offsets() const
{
  return std::atomic_load(&keyOffsets);
}

uint8_t VelocityTable::playedVelocity(uint8_t key, uint8_t velocity, bool pedal) const
{
  std::shared_ptr<const KeyOffsets> offsets = std::atomic_load(&keyOffsets);
  return offsets == nullptr ? velocity : offsets->adjust(key, clampVelocity(velocity), pedal);
}

int16_t VelocityTable::dutyChange(uint8_t key, uint8_t velocity, bool pedal) const
{
  std::shared_ptr<const KeyOffsets> offsets = std::atomic_load(&keyOffsets);
  return offsets == nullptr ? 0 : offsets->dutyChange(key, clampVelocity(velocity), pedal);
}

uint16_t VelocityTable::duty(uint8_t key, uint8_t velocity, bool pedal) const
{
  int32_t duty = tableDuty(key, playedVelocity(key, velocity, pedal), pedal) + dutyChange(key, velocity, pedal);
  return (uint16_t)(duty < 0 ? 0 : duty > DUTY_FULL ? DUTY_FULL : duty);
}

uint16_t VelocityTable::tableDuty(uint8_t key, uint8_t velocity, bool pedal) const
{
  if (!isCalibrated(key))
  {
    return defaultDuty(velocity);
  }
  return layer(key, pedal).duties[key][clampVelocity(velocity) - 1];
}

bool VelocityTable::isCalibrated(uint8_t key) const
{
  return key < PIANO_KEY_COUNT && up.calibrated[key];
}

bool VelocityTable::hasPedal(uint8_t key) const
{
  return down != nullptr && key < PIANO_KEY_COUNT && down->calibrated[key];
}

size_t VelocityTable::pedalKeys() const
{
  size_t count = 0;
  for (uint8_t key = 0; key < PIANO_KEY_COUNT; key++)
  {
    count += hasPedal(key) ? 1 : 0;
  }
  return count;
}

bool VelocityTable::hasLanding() const
{
  if (hasSharedLanding)
  {
    return true;
  }
  for (uint8_t key = 0; key < PIANO_KEY_COUNT; key++)
  {
    if (up.keyHasLanding[key] || hasPedal(key))
    {
      return true;
    }
  }
  return false;
}

uint16_t VelocityTable::landingMs(uint8_t key, uint8_t velocity, bool pedal) const
{
  if (dutyChange(key, velocity, pedal) == 0)
  {
    return tableLandingMs(key, playedVelocity(key, velocity, pedal), pedal);
  }
  return landingAtDuty(key, duty(key, velocity, pedal), pedal);
}

// Along the key's own table: the landing at the first velocity whose duty
// reaches this one, interpolated from the velocity below; the softest's
// below its softest duty, the loudest's above its loudest.
uint16_t VelocityTable::landingAlong(uint8_t key, uint16_t duty, bool pedal) const
{
  uint8_t velocity = 1;
  while (velocity < VELOCITY_LEVELS && tableDuty(key, velocity, pedal) < duty)
  {
    velocity++;
  }
  uint16_t atDuty = tableDuty(key, velocity, pedal), landing = tableLandingMs(key, velocity, pedal);
  if (velocity == 1 || atDuty <= duty)
  {
    return landing; // the softest, the loudest, or exactly this duty
  }
  uint16_t belowDuty = tableDuty(key, velocity - 1, pedal), below = tableLandingMs(key, velocity - 1, pedal);
  int32_t span = atDuty - belowDuty;
  return (uint16_t)lround(below + ((double)landing - below) * (duty - belowDuty) / span);
}

uint16_t VelocityTable::landingAtDuty(uint8_t key, uint16_t duty, bool pedal) const
{
  uint16_t top = tableDuty(key, VELOCITY_LEVELS, pedal), topLanding = tableLandingMs(key, VELOCITY_LEVELS, pedal);
  if (duty <= top || topLanding == 0)
  {
    return landingAlong(key, duty, pedal);
  }
  // Louder than the table goes for this key: its loudest's landing, made as
  // much sooner as its neighbours' land from that duty to this one (the
  // median, of those measured louder than it); never later than it.
  int32_t sooner[2 * LANDING_NEIGHBOURS];
  uint8_t count = 0;
  for (int neighbour = key - LANDING_NEIGHBOURS; neighbour <= key + LANDING_NEIGHBOURS; neighbour++)
  {
    if (neighbour < 0 || neighbour >= PIANO_KEY_COUNT || neighbour == key || !isCalibrated(neighbour) ||
        !layer(neighbour, pedal).keyHasLanding[neighbour])
    {
      continue;
    }
    uint16_t reach = tableDuty(neighbour, VELOCITY_LEVELS, pedal);
    if (reach <= top)
    {
      continue; // it tells nothing above this key's loudest
    }
    sooner[count++] = (int32_t)landingAlong(neighbour, top, pedal) -
                      landingAlong(neighbour, duty < reach ? duty : reach, pedal);
  }
  if (count == 0)
  {
    return topLanding;
  }
  std::sort(sooner, sooner + count);
  int32_t median = count % 2 ? sooner[count / 2] : (sooner[count / 2 - 1] + sooner[count / 2]) / 2;
  int32_t landing = topLanding - (median > 0 ? median : 0);
  return (uint16_t)(landing < 1 ? 1 : landing);
}

uint16_t VelocityTable::tableLandingMs(uint8_t key, uint8_t velocity, bool pedal) const
{
  size_t index = clampVelocity(velocity) - 1;
  if (key < PIANO_KEY_COUNT && layer(key, pedal).keyHasLanding[key])
  {
    return layer(key, pedal).keyLanding[key][index];
  }
  return hasSharedLanding ? sharedLanding[index] : 0;
}

uint16_t VelocityTable::maxLandingMs() const
{
  uint16_t latest = 0;
  for (uint8_t key = 0; key < PIANO_KEY_COUNT; key++)
  {
    for (uint8_t velocity = 1; velocity <= VELOCITY_LEVELS; velocity++)
    {
      for (bool pedal : {false, true})
      {
        uint16_t ms = tableLandingMs(key, velocity, pedal);
        latest = ms > latest ? ms : latest;
      }
    }
  }
  return latest;
}

void VelocityTable::formatDuty(char *text, size_t size, uint16_t duty)
{
  unsigned whole = duty / 100, hundredths = duty % 100;
  if (hundredths == 0)
  {
    snprintf(text, size, "%u", whole);
  }
  else if (hundredths % 10 == 0)
  {
    snprintf(text, size, "%u.%u", whole, hundredths / 10);
  }
  else
  {
    snprintf(text, size, "%u.%02u", whole, hundredths);
  }
}

void VelocityTable::pressCommand(uint8_t key, uint8_t velocity, char *out, size_t size, bool pedal,
                                 const ClampSettings *repeatClamp) const
{
  strikeCommand(key, duty(key, velocity, pedal), landingMs(key, velocity, pedal), out, size, pedal, repeatClamp);
}

void VelocityTable::strikeCommand(uint8_t key, uint16_t strikeDuty, uint16_t landing, char *out, size_t size,
                                  bool pedal, const ClampSettings *repeatClamp) const
{
  char strike[8];
  formatDuty(strike, sizeof(strike), strikeDuty);
  if (landing == 0)
  {
    snprintf(out, size, "P:%u:%s", key, strike);
    return;
  }
  const int32_t strikeMs = driveSettings.strikeMs;
  char coast[8], hold[8];
  formatDuty(coast, sizeof(coast), coastDuty(key, pedal));
  formatDuty(hold, sizeof(hold), holdDuty(key, strikeDuty, pedal));
  if (repeatClamp != nullptr && repeatClamp->ms == 0)
  {
    repeatClamp = nullptr; // none: a repeat plays like any note
  }
  int32_t clampMs = repeatClamp != nullptr ? repeatClamp->ms : setting(key, KeySetting::ClampMs, strikeDuty, pedal);
  if (clampMs > 0)
  {
    int32_t clampAt = landing + (repeatClamp != nullptr ? repeatClamp->offsetMs
                                                        : setting(key, KeySetting::ClampOffsetMs, strikeDuty, pedal));
    if (clampAt < strikeMs)
    {
      clampAt = strikeMs;
    }
    char clamp[8];
    formatDuty(clamp, sizeof(clamp), repeatClamp != nullptr
                                         ? repeatClamp->duty
                                         : (uint16_t)setting(key, KeySetting::ClampDuty, strikeDuty, pedal));
    snprintf(out, size, "P:%u:%s:%u:%s:%u:%s:%u:%s", key, strike, (unsigned)strikeMs, coast,
             (unsigned)(clampAt - strikeMs), clamp, (unsigned)clampMs, hold);
    return;
  }
  int32_t holdAt = landing + setting(key, KeySetting::CoastOffsetMs, strikeDuty, pedal);
  if (holdAt < strikeMs)
  {
    holdAt = strikeMs;
  }
  snprintf(out, size, "P:%u:%s:%u:%s:%u:%s", key, strike, (unsigned)strikeMs, coast, (unsigned)(holdAt - strikeMs),
           hold);
}

const DriveSettings &VelocityTable::drive() const
{
  return driveSettings;
}

uint16_t VelocityTable::coastDuty(uint8_t key, bool pedal) const
{
  return key < PIANO_KEY_COUNT && layer(key, pedal).keyCoast[key] ? layer(key, pedal).keyCoast[key]
                                                                   : driveSettings.coastDuty;
}

static int32_t driveDefault(const DriveSettings &drive, KeySetting which)
{
  switch (which)
  {
  case KeySetting::CoastOffsetMs:
    return drive.coastOffsetMs;
  case KeySetting::ClampOffsetMs:
    return drive.clampOffsetMs;
  case KeySetting::ClampMs:
    return drive.clampMs;
  case KeySetting::ClampDuty:
    return drive.clampDuty;
  case KeySetting::HoldDuty:
  default:
    return drive.holdDuty;
  }
}

int32_t VelocityTable::setting(uint8_t key, KeySetting which, uint16_t strikeDuty, bool pedal) const
{
  if (key >= PIANO_KEY_COUNT || layer(key, pedal).keySettings[key][(size_t)which].count == 0)
  {
    return driveDefault(driveSettings, which);
  }
  const Steps &steps = layer(key, pedal).keySettings[key][(size_t)which];
  int32_t value = steps.value[0];
  for (uint8_t i = 1; i < steps.count; i++)
  {
    if (strikeDuty > steps.above[i])
    {
      value = steps.value[i];
    }
  }
  return value;
}

uint16_t VelocityTable::holdDuty(uint8_t key, uint16_t strikeDuty, bool pedal) const
{
  return (uint16_t)setting(key, KeySetting::HoldDuty, strikeDuty, pedal);
}

size_t VelocityTable::calibratedKeys() const
{
  size_t count = 0;
  for (bool on : up.calibrated)
  {
    count += on ? 1 : 0;
  }
  return count;
}
