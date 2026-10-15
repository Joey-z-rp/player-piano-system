#include "app/piano/velocity_table.h"
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>

// The parsed table takes ~90 KB; keep it out of the much smaller internal RAM.
struct SpiRamAllocator : ArduinoJson::Allocator
{
  void *allocate(size_t size) override
  {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  }
  void deallocate(void *pointer) override
  {
    heap_caps_free(pointer);
  }
  void *reallocate(void *pointer, size_t size) override
  {
    return heap_caps_realloc(pointer, size, MALLOC_CAP_SPIRAM);
  }
};
static SpiRamAllocator spiRamAllocator;
#define JSON_ALLOCATOR (&spiRamAllocator)
#else
#define JSON_ALLOCATOR ArduinoJson::detail::DefaultAllocator::instance()
#endif

static uint8_t clampVelocity(uint8_t velocity)
{
  if (velocity < 1)
  {
    return 1;
  }
  return velocity > VELOCITY_LEVELS ? VELOCITY_LEVELS : velocity;
}

VelocityTable::VelocityTable()
    : duties(), calibrated(), keyLanding(), keyHasLanding(), sharedLanding(), hasSharedLanding(false),
      driveSettings(), keyCoast(), keySettings()
{
}

// Reads a "landing_ms" list of 127 times into out.
static bool readLanding(JsonVariant list, uint16_t *out, const std::string &where, std::string &error)
{
  JsonArray values = list.as<JsonArray>();
  if (values.isNull() || values.size() != VELOCITY_LEVELS)
  {
    error = where + "\"landing_ms\" must list 127 values";
    return false;
  }
  size_t index = 0;
  for (JsonVariant value : values)
  {
    if (!value.is<double>() || value.as<double>() < 0 || value.as<double>() > MAX_LANDING_MS)
    {
      error = where + "landing times must be numbers 0-" + std::to_string(MAX_LANDING_MS);
      return false;
    }
    out[index++] = (uint16_t)lround(value.as<double>());
  }
  return true;
}

uint16_t VelocityTable::defaultDuty(uint8_t velocity)
{
  velocity = clampVelocity(velocity);
  return DEFAULT_MIN_DUTY * 100 + (uint32_t)(velocity - 1) * (DEFAULT_MAX_DUTY - DEFAULT_MIN_DUTY) * 100 / (VELOCITY_LEVELS - 1);
}

uint16_t VelocityTable::duty(uint8_t key, uint8_t velocity) const
{
  if (!isCalibrated(key))
  {
    return defaultDuty(velocity);
  }
  return duties[key][clampVelocity(velocity) - 1];
}

bool VelocityTable::isCalibrated(uint8_t key) const
{
  return key < PIANO_KEY_COUNT && calibrated[key];
}

bool VelocityTable::hasLanding() const
{
  if (hasSharedLanding)
  {
    return true;
  }
  for (bool own : keyHasLanding)
  {
    if (own)
    {
      return true;
    }
  }
  return false;
}

uint16_t VelocityTable::landingMs(uint8_t key, uint8_t velocity) const
{
  size_t index = clampVelocity(velocity) - 1;
  if (key < PIANO_KEY_COUNT && keyHasLanding[key])
  {
    return keyLanding[key][index];
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
      uint16_t ms = landingMs(key, velocity);
      latest = ms > latest ? ms : latest;
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

void VelocityTable::pressCommand(uint8_t key, uint8_t velocity, char *out, size_t size) const
{
  uint16_t strikeDuty = duty(key, velocity);
  char strike[8];
  formatDuty(strike, sizeof(strike), strikeDuty);
  uint16_t landing = landingMs(key, velocity);
  if (landing == 0)
  {
    snprintf(out, size, "P:%u:%s", key, strike);
    return;
  }
  const int32_t strikeMs = driveSettings.strikeMs;
  char coast[8], hold[8];
  formatDuty(coast, sizeof(coast), coastDuty(key));
  formatDuty(hold, sizeof(hold), holdDuty(key, strikeDuty));
  int32_t clampMs = setting(key, KeySetting::ClampMs, strikeDuty);
  if (clampMs > 0)
  {
    int32_t clampAt = landing + setting(key, KeySetting::ClampOffsetMs, strikeDuty);
    if (clampAt < strikeMs)
    {
      clampAt = strikeMs;
    }
    char clamp[8];
    formatDuty(clamp, sizeof(clamp), (uint16_t)setting(key, KeySetting::ClampDuty, strikeDuty));
    snprintf(out, size, "P:%u:%s:%u:%s:%u:%s:%u:%s", key, strike, (unsigned)strikeMs, coast,
             (unsigned)(clampAt - strikeMs), clamp, (unsigned)clampMs, hold);
    return;
  }
  int32_t holdAt = landing + setting(key, KeySetting::CoastOffsetMs, strikeDuty);
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

uint16_t VelocityTable::coastDuty(uint8_t key) const
{
  return key < PIANO_KEY_COUNT && keyCoast[key] ? keyCoast[key] : driveSettings.coastDuty;
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

int32_t VelocityTable::setting(uint8_t key, KeySetting which, uint16_t strikeDuty) const
{
  if (key >= PIANO_KEY_COUNT || keySettings[key][(size_t)which].count == 0)
  {
    return driveDefault(driveSettings, which);
  }
  const Steps &steps = keySettings[key][(size_t)which];
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

uint16_t VelocityTable::holdDuty(uint8_t key, uint16_t strikeDuty) const
{
  return (uint16_t)setting(key, KeySetting::HoldDuty, strikeDuty);
}

// Reads a duty in percent (up to 100) as hundredths, or a whole number of ms
// within [low, high]. found says whether there was a value.
static bool readValue(JsonVariant value, const std::string &name, bool isDuty, long low, long high, int32_t &out,
                      std::string &error)
{
  if (isDuty)
  {
    if (!value.is<double>() || value.as<double>() < low || value.as<double>() > high)
    {
      error = name + " must be a duty from " + std::to_string(low) + " to " + std::to_string(high);
      return false;
    }
    out = (int32_t)lround(value.as<double>() * 100);
    return true;
  }
  if (!value.is<long>() || value.as<long>() < low || value.as<long>() > high)
  {
    error = name + " must be a whole number of ms from " + std::to_string(low) + " to " + std::to_string(high);
    return false;
  }
  out = (int32_t)value.as<long>();
  return true;
}

// Reads an optional setting that's one value, or [[strike duty above which,
// value], ...] in rising order, into steps (count 0 if absent).
static bool readSteps(JsonVariant value, const std::string &name, bool isDuty, long low, long high, uint16_t above[],
                      int32_t values[], uint8_t &count, std::string &error)
{
  count = 0;
  if (value.isNull())
  {
    return true;
  }
  if (!value.is<JsonArray>())
  {
    above[0] = 0;
    count = 1;
    return readValue(value, name, isDuty, low, high, values[0], error);
  }
  std::string bad = name + " steps must be up to " + std::to_string(MAX_SETTING_STEPS) +
                    " [strike duty, value] pairs in rising order";
  JsonArray list = value.as<JsonArray>();
  if (list.size() == 0 || list.size() > MAX_SETTING_STEPS)
  {
    error = bad;
    return false;
  }
  for (JsonVariant step : list)
  {
    JsonArray pair = step.as<JsonArray>();
    if (pair.isNull() || pair.size() != 2 || !pair[0].is<double>() || pair[0].as<double>() < 0 ||
        pair[0].as<double>() > 100 || (count > 0 && lround(pair[0].as<double>() * 100) <= above[count - 1]))
    {
      error = bad;
      return false;
    }
    if (!readValue(pair[1], name, isDuty, low, high, values[count], error))
    {
      return false;
    }
    above[count] = (uint16_t)lround(pair[0].as<double>() * 100);
    count++;
  }
  return true;
}

// The key settings as the table names them, and their limits.
struct SettingSpec
{
  const char *name;
  bool isDuty;
  long low, high;
};
static const SettingSpec SETTING_SPECS[(size_t)KeySetting::Count] = {
    {"coast_offset_ms", false, -500, 500},
    {"clamp_offset_ms", false, -500, 500},
    {"clamp_ms", false, 0, 1000},
    {"clamp_duty", true, 0, 100},
    {"hold", true, 0, 100},
};

// Reads the table's optional "drive" over the defaults.
static bool readDrive(JsonVariant value, DriveSettings &drive, std::string &error)
{
  if (value.isNull())
  {
    return true;
  }
  if (!value.is<JsonObject>())
  {
    error = "\"drive\" must be an object";
    return false;
  }
  struct Field
  {
    const char *name;
    bool isDuty;
    long low, high;
    int32_t *out;
  };
  int32_t strikeMs = drive.strikeMs, coastDuty = drive.coastDuty, coastOffset = drive.coastOffsetMs,
          clampOffset = drive.clampOffsetMs, clampMs = drive.clampMs, clampDuty = drive.clampDuty,
          holdDuty = drive.holdDuty;
  const Field fields[] = {
      {"strike_ms", false, 1, 1000, &strikeMs},           {"coast_duty", true, 0, 100, &coastDuty},
      {"coast_offset_ms", false, -500, 500, &coastOffset}, {"clamp_offset_ms", false, -500, 500, &clampOffset},
      {"clamp_ms", false, 0, 1000, &clampMs},              {"clamp_duty", true, 0, 100, &clampDuty},
      {"hold_duty", true, 0, 100, &holdDuty},
  };
  for (const Field &field : fields)
  {
    JsonVariant given = value[field.name];
    if (!given.isNull() &&
        !readValue(given, std::string("drive ") + field.name, field.isDuty, field.low, field.high, *field.out, error))
    {
      return false;
    }
  }
  drive.strikeMs = (uint16_t)strikeMs;
  drive.coastDuty = (uint16_t)coastDuty;
  drive.coastOffsetMs = (int16_t)coastOffset;
  drive.clampOffsetMs = (int16_t)clampOffset;
  drive.clampMs = (uint16_t)clampMs;
  drive.clampDuty = (uint16_t)clampDuty;
  drive.holdDuty = (uint16_t)holdDuty;
  return true;
}

size_t VelocityTable::calibratedKeys() const
{
  size_t count = 0;
  for (bool on : calibrated)
  {
    count += on ? 1 : 0;
  }
  return count;
}

bool VelocityTable::load(const char *json, size_t length, std::string &error)
{
  JsonDocument filter;
  filter["keys"]["*"]["duty"] = true;
  filter["keys"]["*"]["landing_ms"] = true;
  filter["keys"]["*"]["coast"] = true;
  for (const SettingSpec &spec : SETTING_SPECS)
  {
    filter["keys"]["*"][spec.name] = true;
  }
  filter["drive"] = true;
  filter["landing_ms"] = true;
  JsonDocument doc(JSON_ALLOCATOR);
  DeserializationError parsed = deserializeJson(doc, json, length, DeserializationOption::Filter(filter));
  if (parsed)
  {
    error = std::string("not valid JSON (") + parsed.c_str() + ")";
    return false;
  }
  JsonObject keys = doc["keys"];
  if (keys.isNull())
  {
    error = "no \"keys\" object";
    return false;
  }

  // Filled separately, so a bad entry leaves the current table as it was.
  std::vector<uint16_t> nextDuties(PIANO_KEY_COUNT * VELOCITY_LEVELS);
  bool nextCalibrated[PIANO_KEY_COUNT] = {};
  std::vector<uint16_t> nextKeyLanding(PIANO_KEY_COUNT * VELOCITY_LEVELS);
  bool nextKeyHasLanding[PIANO_KEY_COUNT] = {};
  uint16_t nextCoast[PIANO_KEY_COUNT] = {};
  std::vector<Steps> nextSettings(PIANO_KEY_COUNT * (size_t)KeySetting::Count, Steps());
  for (JsonPair pair : keys)
  {
    const char *name = pair.key().c_str();
    char *end = nullptr;
    long key = strtol(name, &end, 10);
    if (*name == '\0' || *end != '\0' || key < 0 || key >= PIANO_KEY_COUNT)
    {
      error = std::string("key \"") + name + "\" is not 0-87";
      return false;
    }
    JsonArray values = pair.value()["duty"];
    if (values.isNull() || values.size() != VELOCITY_LEVELS)
    {
      error = "key " + std::to_string(key) + ": \"duty\" must list 127 values";
      return false;
    }
    size_t index = 0;
    for (JsonVariant value : values)
    {
      if (!value.is<double>() || value.as<double>() < 0 || value.as<double>() > 100)
      {
        error = "key " + std::to_string(key) + ": duties must be numbers 0-100";
        return false;
      }
      nextDuties[key * VELOCITY_LEVELS + index++] = (uint16_t)lround(value.as<double>() * 100);
    }
    nextCalibrated[key] = true;

    JsonVariant ownLanding = pair.value()["landing_ms"];
    if (!ownLanding.isNull())
    {
      if (!readLanding(ownLanding, &nextKeyLanding[key * VELOCITY_LEVELS], "key " + std::to_string(key) + ": ",
                       error))
      {
        return false;
      }
      nextKeyHasLanding[key] = true;
    }
    JsonVariant coast = pair.value()["coast"];
    if (!coast.isNull())
    {
      int32_t coastDuty = 0;
      if (!readValue(coast, "key " + std::to_string(key) + ": coast", true, 0, 100, coastDuty, error))
      {
        return false;
      }
      if (coastDuty == 0)
      {
        error = "key " + std::to_string(key) + ": coast must be above 0";
        return false;
      }
      nextCoast[key] = (uint16_t)coastDuty;
    }
    for (size_t which = 0; which < (size_t)KeySetting::Count; which++)
    {
      const SettingSpec &spec = SETTING_SPECS[which];
      Steps &steps = nextSettings[key * (size_t)KeySetting::Count + which];
      if (!readSteps(pair.value()[spec.name], "key " + std::to_string(key) + ": " + spec.name, spec.isDuty, spec.low,
                     spec.high, steps.above, steps.value, steps.count, error))
      {
        return false;
      }
    }
  }

  uint16_t nextShared[VELOCITY_LEVELS] = {};
  JsonVariant sharedList = doc["landing_ms"];
  bool nextHasShared = !sharedList.isNull();
  if (nextHasShared && !readLanding(sharedList, nextShared, "", error))
  {
    return false;
  }
  DriveSettings nextDrive;
  if (!readDrive(doc["drive"], nextDrive, error))
  {
    return false;
  }

  memcpy(duties, nextDuties.data(), sizeof(duties));
  memcpy(calibrated, nextCalibrated, sizeof(calibrated));
  memcpy(keyLanding, nextKeyLanding.data(), sizeof(keyLanding));
  memcpy(keyHasLanding, nextKeyHasLanding, sizeof(keyHasLanding));
  memcpy(sharedLanding, nextShared, sizeof(sharedLanding));
  hasSharedLanding = nextHasShared;
  memcpy(keyCoast, nextCoast, sizeof(keyCoast));
  memcpy(keySettings, nextSettings.data(), sizeof(keySettings));
  driveSettings = nextDrive;
  return true;
}
