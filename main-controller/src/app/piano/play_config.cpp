#include "app/piano/play_config.h"
#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

namespace
{
  // A whole number within [low, high], if given.
  bool readNumber(JsonVariant value, const char *name, long low, long high, long &out, std::string &error)
  {
    if (value.isNull())
    {
      return true;
    }
    if (!value.is<long>() || value.as<long>() < low || value.as<long>() > high)
    {
      error = std::string(name) + " must be a whole number from " + std::to_string(low) + " to " +
              std::to_string(high);
      return false;
    }
    out = value.as<long>();
    return true;
  }

  template <typename T>
  bool readInto(JsonVariant value, const char *name, long low, long high, T &out, std::string &error)
  {
    long read = out;
    if (!readNumber(value, name, low, high, read, error))
    {
      return false;
    }
    out = (T)read;
    return true;
  }

  // A pedal command's settings after "P:<channel>:": numbers and colons.
  bool readPedalCommand(JsonVariant value, const char *name, char *out, std::string &error)
  {
    if (value.isNull())
    {
      return true;
    }
    const char *text = value.as<const char *>();
    size_t length = text == nullptr ? 0 : strlen(text);
    bool valid = length > 0 && length < PEDAL_COMMAND_LENGTH && isdigit((unsigned char)text[0]);
    for (size_t i = 0; valid && i < length; i++)
    {
      valid = isdigit((unsigned char)text[i]) || text[i] == ':' || text[i] == '.';
    }
    if (!valid)
    {
      error = std::string(name) + " must be the command's settings, e.g. 100:30:100:30:55";
      return false;
    }
    strcpy(out, text);
    return true;
  }
} // namespace

bool readPlayConfig(const char *json, size_t length, PlayConfig &config, std::string &error)
{
  JsonDocument doc;
  DeserializationError parsed = deserializeJson(doc, json, length);
  if (parsed || !doc.is<JsonObject>())
  {
    error = "settings must be a JSON object";
    return false;
  }
  PlayConfig next = config;
  if (!readInto(doc["min_note_ms"], "min_note_ms", 0, 500, next.minNoteMs, error) ||
      !readInto(doc["restrike_gap_ms"], "restrike_gap_ms", 0, 500, next.restrikeGapMs, error) ||
      !readInto(doc["repeat_loud_from"], "repeat_loud_from", 1, 127, next.repeatLoudFrom, error) ||
      !readInto(doc["repeat_window_ms"], "repeat_window_ms", 0, 2000, next.repeatWindowMs, error) ||
      !readInto(doc["repeat_late_ms"], "repeat_late_ms", 0, 100, next.repeatLateMs, error) ||
      !readInto(doc["repeat_clamp_offset_ms"], "repeat_clamp_offset_ms", -200, 200, next.repeatClamp.offsetMs, error) ||
      !readInto(doc["repeat_clamp_ms"], "repeat_clamp_ms", 0, 1000, next.repeatClamp.ms, error) ||
      !readInto(doc["pedal_settle_ms"], "pedal_settle_ms", 0, 1000, next.pedalSettleMs, error) ||
      !readInto(doc["pedal_min_ms"], "pedal_min_ms", 0, 1000, next.pedalMinMs, error) ||
      !readPedalCommand(doc["pedal_press"], "pedal_press", next.pedalPress, error) ||
      !readPedalCommand(doc["pedal_release"], "pedal_release", next.pedalRelease, error))
  {
    return false;
  }
  JsonVariant clampDuty = doc["repeat_clamp_duty"];
  if (!clampDuty.isNull())
  {
    if (!clampDuty.is<double>() || clampDuty.as<double>() < 0 || clampDuty.as<double>() > 100)
    {
      error = "repeat_clamp_duty must be a duty from 0 to 100";
      return false;
    }
    next.repeatClamp.duty = (uint16_t)lround(clampDuty.as<double>() * 100);
  }
  JsonVariant zone = doc["repeat_dead_zone"];
  if (!zone.isNull())
  {
    JsonArray values = zone.as<JsonArray>();
    if (values.isNull() || values.size() != 4)
    {
      error = "repeat_dead_zone must be [from velocity, to velocity, from ms, to ms]";
      return false;
    }
    if (!readInto(values[0], "the dead zone's from velocity", 0, 127, next.deadZoneFromVelocity, error) ||
        !readInto(values[1], "the dead zone's to velocity", 0, 127, next.deadZoneToVelocity, error) ||
        !readInto(values[2], "the dead zone's from ms", 0, 2000, next.deadZoneFromMs, error) ||
        !readInto(values[3], "the dead zone's to ms", 0, 2000, next.deadZoneToMs, error))
    {
      return false;
    }
  }
  JsonVariant limits = doc["repeat_limits"];
  if (!limits.isNull())
  {
    JsonArray rows = limits.as<JsonArray>();
    std::string bad = "repeat_limits must be 1-" + std::to_string(MAX_REPEAT_LIMITS) +
                      " [from velocity, after soft ms, after loud ms] in falling order of velocity, the last from 0";
    if (rows.isNull() || rows.size() == 0 || rows.size() > MAX_REPEAT_LIMITS)
    {
      error = bad;
      return false;
    }
    uint8_t count = 0;
    for (JsonVariant row : rows)
    {
      JsonArray cells = row.as<JsonArray>();
      RepeatLimit &limit = next.repeatLimits[count];
      if (cells.isNull() || cells.size() != 3 || !readInto(cells[0], "a limit's velocity", 0, 127, limit.from, error) ||
          !readInto(cells[1], "a limit's ms after a soft note", 0, 2000, limit.afterSoftMs, error) ||
          !readInto(cells[2], "a limit's ms after a loud note", 0, 2000, limit.afterLoudMs, error) ||
          (count > 0 && limit.from >= next.repeatLimits[count - 1].from))
      {
        if (error.empty())
        {
          error = bad;
        }
        return false;
      }
      count++;
    }
    if (next.repeatLimits[count - 1].from != 0)
    {
      error = bad;
      return false;
    }
    next.repeatLimitCount = count;
  }
  config = next;
  return true;
}

std::string playConfigJson(const PlayConfig &config)
{
  JsonDocument doc;
  doc["min_note_ms"] = config.minNoteMs;
  doc["restrike_gap_ms"] = config.restrikeGapMs;
  JsonArray limits = doc["repeat_limits"].to<JsonArray>();
  for (uint8_t i = 0; i < config.repeatLimitCount; i++)
  {
    JsonArray row = limits.add<JsonArray>();
    row.add(config.repeatLimits[i].from);
    row.add(config.repeatLimits[i].afterSoftMs);
    row.add(config.repeatLimits[i].afterLoudMs);
  }
  doc["repeat_loud_from"] = config.repeatLoudFrom;
  JsonArray zone = doc["repeat_dead_zone"].to<JsonArray>();
  zone.add(config.deadZoneFromVelocity);
  zone.add(config.deadZoneToVelocity);
  zone.add(config.deadZoneFromMs);
  zone.add(config.deadZoneToMs);
  doc["repeat_window_ms"] = config.repeatWindowMs;
  doc["repeat_late_ms"] = config.repeatLateMs;
  doc["repeat_clamp_offset_ms"] = config.repeatClamp.offsetMs;
  doc["repeat_clamp_ms"] = config.repeatClamp.ms;
  doc["repeat_clamp_duty"] = config.repeatClamp.duty / 100.0;
  doc["pedal_settle_ms"] = config.pedalSettleMs;
  doc["pedal_min_ms"] = config.pedalMinMs;
  doc["pedal_press"] = config.pedalPress;
  doc["pedal_release"] = config.pedalRelease;
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool operator==(const PlayConfig &a, const PlayConfig &b)
{
  return playConfigJson(a) == playConfigJson(b);
}
