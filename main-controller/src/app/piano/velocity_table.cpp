#include "app/piano/velocity_table.h"
#include <ArduinoJson.h>
#include <math.h>
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

VelocityTable::VelocityTable() : duties(), calibrated()
{
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
  }

  memcpy(duties, nextDuties.data(), sizeof(duties));
  memcpy(calibrated, nextCalibrated, sizeof(calibrated));
  return true;
}
