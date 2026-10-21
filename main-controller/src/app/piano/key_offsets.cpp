#include "app/piano/key_offsets.h"
#include <ArduinoJson.h>

namespace
{
  // n / d rounded half away from zero (d > 0).
  int32_t divideRounded(int32_t n, int32_t d)
  {
    return n >= 0 ? (n + d / 2) / d : -((-n + d / 2) / d);
  }

  bool readCurve(JsonVariant value, OffsetCurve &out, std::string &error)
  {
    JsonArray points = value.as<JsonArray>();
    if (points.isNull() || points.size() > MAX_OFFSET_POINTS)
    {
      error = "an offset curve must be a list of up to " + std::to_string(MAX_OFFSET_POINTS) +
              " [velocity, offset] points";
      return false;
    }
    OffsetCurve curve;
    for (JsonVariant point : points)
    {
      JsonArray p = point.as<JsonArray>();
      if (p.isNull() || p.size() != 2 || !p[0].is<long>() || !p[1].is<long>() || p[0].as<long>() < 1 ||
          p[0].as<long>() > TOP_VELOCITY || p[1].as<long>() < -MAX_VELOCITY_OFFSET ||
          p[1].as<long>() > MAX_VELOCITY_OFFSET)
      {
        error = "each point must be [velocity 1-127, offset -" + std::to_string(MAX_VELOCITY_OFFSET) + " to " +
                std::to_string(MAX_VELOCITY_OFFSET) + "]";
        return false;
      }
      uint8_t velocity = (uint8_t)p[0].as<long>();
      if (curve.count > 0 && velocity <= curve.points[curve.count - 1].velocity)
      {
        error = "the points must be in rising order of velocity";
        return false;
      }
      curve.points[curve.count++] = {velocity, (int8_t)p[1].as<long>()};
    }
    for (uint8_t v = 2; v <= TOP_VELOCITY; v++)
    {
      if (curve.adjust(v) < curve.adjust(v - 1))
      {
        error = "velocity " + std::to_string(v) + " would play softer than " + std::to_string(v - 1) +
                ": an offset can't fall by more than 1 per velocity";
        return false;
      }
    }
    out = curve;
    return true;
  }

  void writeCurve(JsonArray out, const OffsetCurve &curve)
  {
    for (uint8_t i = 0; i < curve.count; i++)
    {
      JsonArray point = out.add<JsonArray>();
      point.add(curve.points[i].velocity);
      point.add(curve.points[i].offset);
    }
  }
} // namespace

uint8_t OffsetCurve::adjust(uint8_t velocity) const
{
  if (count == 0)
  {
    return velocity;
  }
  int32_t offset;
  if (velocity <= points[0].velocity)
  {
    offset = points[0].offset;
  }
  else if (velocity >= points[count - 1].velocity)
  {
    offset = points[count - 1].offset;
  }
  else
  {
    uint8_t i = 1;
    while (points[i].velocity < velocity)
    {
      i++;
    }
    const OffsetPoint &a = points[i - 1], &b = points[i];
    int32_t span = b.velocity - a.velocity;
    offset = divideRounded(a.offset * span + (b.offset - a.offset) * (velocity - a.velocity), span);
  }
  int32_t played = velocity + offset;
  return (uint8_t)(played < 1 ? 1 : played > TOP_VELOCITY ? TOP_VELOCITY : played);
}

bool OffsetCurve::operator==(const OffsetCurve &other) const
{
  if (count != other.count)
  {
    return false;
  }
  for (uint8_t i = 0; i < count; i++)
  {
    if (points[i].velocity != other.points[i].velocity || points[i].offset != other.points[i].offset)
    {
      return false;
    }
  }
  return true;
}

uint8_t KeyOffsets::adjust(uint8_t key, uint8_t velocity, bool pedalDown) const
{
  if (key >= PIANO_KEY_COUNT)
  {
    return velocity;
  }
  return (pedalDown && hasPedal[key] ? pedal[key] : up[key]).adjust(velocity);
}

bool KeyOffsets::empty() const
{
  for (uint8_t key = 0; key < PIANO_KEY_COUNT; key++)
  {
    if (up[key].count > 0 || hasPedal[key])
    {
      return false;
    }
  }
  return true;
}

bool readOffsetCurve(const char *json, size_t length, OffsetCurve &curve, std::string &error)
{
  JsonDocument doc;
  if (deserializeJson(doc, json, length))
  {
    error = "an offset curve must be JSON";
    return false;
  }
  return readCurve(doc.as<JsonVariant>(), curve, error);
}

bool readKeyOffsets(const char *json, size_t length, KeyOffsets &offsets, std::string &error)
{
  JsonDocument doc;
  if (deserializeJson(doc, json, length) || !doc["keys"].is<JsonObject>())
  {
    error = "offsets must be {\"keys\": {...}}";
    return false;
  }
  KeyOffsets next;
  for (JsonPair entry : doc["keys"].as<JsonObject>())
  {
    char *end = nullptr;
    long key = strtol(entry.key().c_str(), &end, 10);
    if (*end != '\0' || key < 0 || key >= PIANO_KEY_COUNT || !entry.value().is<JsonObject>())
    {
      error = std::string("\"") + entry.key().c_str() + "\" isn't a key from 0 to 87";
      return false;
    }
    std::string where = "key " + std::to_string(key) + ": ";
    JsonVariant up = entry.value()["up"], pedal = entry.value()["pedal"];
    if ((!up.isNull() && !readCurve(up, next.up[key], error)) ||
        (!pedal.isNull() && !readCurve(pedal, next.pedal[key], error)))
    {
      error = where + error;
      return false;
    }
    next.hasPedal[key] = !pedal.isNull();
  }
  offsets = next;
  return true;
}

std::string offsetCurveJson(const OffsetCurve &curve)
{
  JsonDocument doc;
  writeCurve(doc.to<JsonArray>(), curve);
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string keyOffsetsJson(const KeyOffsets &offsets)
{
  JsonDocument doc;
  JsonObject keys = doc["keys"].to<JsonObject>();
  for (uint8_t key = 0; key < PIANO_KEY_COUNT; key++)
  {
    if (offsets.up[key].count == 0 && !offsets.hasPedal[key])
    {
      continue;
    }
    JsonObject entry = keys[std::to_string(key)].to<JsonObject>();
    writeCurve(entry["up"].to<JsonArray>(), offsets.up[key]);
    if (offsets.hasPedal[key])
    {
      writeCurve(entry["pedal"].to<JsonArray>(), offsets.pedal[key]);
    }
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}
