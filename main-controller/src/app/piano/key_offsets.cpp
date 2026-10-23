#include "app/piano/key_offsets.h"
#include <ArduinoJson.h>
#include <limits.h>
#include <math.h>
#include <memory>

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

  bool readDutyCurve(JsonVariant value, DutyCurve &out, std::string &error)
  {
    JsonArray points = value.as<JsonArray>();
    if (points.isNull() || points.size() > MAX_OFFSET_POINTS)
    {
      error = "a duty curve must be a list of up to " + std::to_string(MAX_OFFSET_POINTS) +
              " [velocity, duty change] points";
      return false;
    }
    DutyCurve curve;
    for (JsonVariant point : points)
    {
      JsonArray p = point.as<JsonArray>();
      long change = p.isNull() || p.size() != 2 || !p[1].is<double>() ? LONG_MAX : lround(p[1].as<double>() * 100);
      if (p.isNull() || p.size() != 2 || !p[0].is<long>() || p[0].as<long>() < 1 ||
          p[0].as<long>() > TOP_VELOCITY || change < -MAX_DUTY_CHANGE || change > MAX_DUTY_CHANGE ||
          change % DUTY_CHANGE_STEP != 0)
      {
        error = "each point must be [velocity 1-127, duty change -" + std::to_string(MAX_DUTY_CHANGE / 100) +
                " to " + std::to_string(MAX_DUTY_CHANGE / 100) + "% in 0.25% steps]";
        return false;
      }
      uint8_t velocity = (uint8_t)p[0].as<long>();
      if (curve.count > 0 && velocity <= curve.points[curve.count - 1].velocity)
      {
        error = "the points must be in rising order of velocity";
        return false;
      }
      curve.points[curve.count++] = {velocity, (int16_t)change};
    }
    out = curve;
    return true;
  }

  // A key's entry (see readKeyEntry).
  bool readEntry(JsonVariant entry, KeyOffsets::Entry &out, std::string &error)
  {
    if (!entry.is<JsonObject>())
    {
      error = "a key's entry must be {\"up\": [...], ...}";
      return false;
    }
    JsonVariant up = entry["up"], pedal = entry["pedal"], duty = entry["duty"], dutyPedal = entry["duty_pedal"];
    KeyOffsets::Entry read;
    if ((!up.isNull() && !readCurve(up, read.up, error)) || (!pedal.isNull() && !readCurve(pedal, read.pedal, error)))
    {
      return false;
    }
    if ((!duty.isNull() && !readDutyCurve(duty, read.dutyUp, error)) ||
        (!dutyPedal.isNull() && !readDutyCurve(dutyPedal, read.dutyPedal, error)))
    {
      error = "duty: " + error;
      return false;
    }
    read.hasPedal = !pedal.isNull();
    read.hasDutyPedal = !dutyPedal.isNull();
    out = read;
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

  void writeDutyCurve(JsonArray out, const DutyCurve &curve)
  {
    for (uint8_t i = 0; i < curve.count; i++)
    {
      JsonArray point = out.add<JsonArray>();
      point.add(curve.points[i].velocity);
      point.add(curve.points[i].change / 100.0);
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

int16_t DutyCurve::change(uint8_t velocity) const
{
  if (count == 0)
  {
    return 0;
  }
  if (velocity <= points[0].velocity)
  {
    return points[0].change;
  }
  if (velocity >= points[count - 1].velocity)
  {
    return points[count - 1].change;
  }
  uint8_t i = 1;
  while (points[i].velocity < velocity)
  {
    i++;
  }
  const DutyPoint &a = points[i - 1], &b = points[i];
  int32_t span = b.velocity - a.velocity;
  int32_t n = a.change * span + (b.change - a.change) * (velocity - a.velocity);
  return (int16_t)(divideRounded(n, span * DUTY_CHANGE_STEP) * DUTY_CHANGE_STEP);
}

bool DutyCurve::operator==(const DutyCurve &other) const
{
  if (count != other.count)
  {
    return false;
  }
  for (uint8_t i = 0; i < count; i++)
  {
    if (points[i].velocity != other.points[i].velocity || points[i].change != other.points[i].change)
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

int16_t KeyOffsets::dutyChange(uint8_t key, uint8_t velocity, bool pedalDown) const
{
  if (key >= PIANO_KEY_COUNT)
  {
    return 0;
  }
  return (pedalDown && hasDutyPedal[key] ? dutyPedal[key] : dutyUp[key]).change(velocity);
}

bool KeyOffsets::has(uint8_t key) const
{
  return key < PIANO_KEY_COUNT &&
         (up[key].count > 0 || hasPedal[key] || dutyUp[key].count > 0 || hasDutyPedal[key]);
}

bool KeyOffsets::empty() const
{
  for (uint8_t key = 0; key < PIANO_KEY_COUNT; key++)
  {
    if (has(key))
    {
      return false;
    }
  }
  return true;
}

KeyOffsets::Entry KeyOffsets::entry(uint8_t key) const
{
  Entry out;
  if (key < PIANO_KEY_COUNT)
  {
    out.up = up[key];
    out.pedal = pedal[key];
    out.hasPedal = hasPedal[key];
    out.dutyUp = dutyUp[key];
    out.dutyPedal = dutyPedal[key];
    out.hasDutyPedal = hasDutyPedal[key];
  }
  return out;
}

void KeyOffsets::set(uint8_t key, const Entry &entry)
{
  if (key >= PIANO_KEY_COUNT)
  {
    return;
  }
  up[key] = entry.up;
  hasPedal[key] = entry.hasPedal;
  pedal[key] = entry.hasPedal ? entry.pedal : OffsetCurve();
  dutyUp[key] = entry.dutyUp;
  hasDutyPedal[key] = entry.hasDutyPedal;
  dutyPedal[key] = entry.hasDutyPedal ? entry.dutyPedal : DutyCurve();
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
  std::unique_ptr<KeyOffsets> next(new KeyOffsets()); // ~9 KB: not on the stack
  for (JsonPair entry : doc["keys"].as<JsonObject>())
  {
    char *end = nullptr;
    long key = strtol(entry.key().c_str(), &end, 10);
    if (*end != '\0' || key < 0 || key >= PIANO_KEY_COUNT)
    {
      error = std::string("\"") + entry.key().c_str() + "\" isn't a key from 0 to 87";
      return false;
    }
    KeyOffsets::Entry read;
    if (!readEntry(entry.value(), read, error))
    {
      error = "key " + std::to_string(key) + ": " + error;
      return false;
    }
    next->set((uint8_t)key, read);
  }
  offsets = *next;
  return true;
}

bool readKeyEntry(const char *json, size_t length, KeyOffsets::Entry &entry, std::string &error)
{
  JsonDocument doc;
  if (deserializeJson(doc, json, length))
  {
    error = "a key's entry must be JSON";
    return false;
  }
  return readEntry(doc.as<JsonVariant>(), entry, error);
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
    if (!offsets.has(key))
    {
      continue;
    }
    JsonObject entry = keys[std::to_string(key)].to<JsonObject>();
    writeCurve(entry["up"].to<JsonArray>(), offsets.up[key]);
    if (offsets.hasPedal[key])
    {
      writeCurve(entry["pedal"].to<JsonArray>(), offsets.pedal[key]);
    }
    if (offsets.dutyUp[key].count > 0)
    {
      writeDutyCurve(entry["duty"].to<JsonArray>(), offsets.dutyUp[key]);
    }
    if (offsets.hasDutyPedal[key])
    {
      writeDutyCurve(entry["duty_pedal"].to<JsonArray>(), offsets.dutyPedal[key]);
    }
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}
