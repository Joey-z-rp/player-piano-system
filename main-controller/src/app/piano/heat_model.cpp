#include "app/piano/heat_model.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t DEFAULT_STRIKE_MS = 50; // INITIAL_STRIKE_TIME_MS in the driver
static const float DEFAULT_HOLD = 0.20f;      // HOLD_DUTY in the driver

HeatModel::HeatModel()
{
}

void HeatModel::configure(const HeatSettings &settings)
{
  config = settings;
}

const HeatSettings &HeatModel::settings() const
{
  return config;
}

const CoilSettings &HeatModel::coil(uint8_t channel) const
{
  return channel >= FIRST_PEDAL_CHANNEL ? config.pedal : config.keys;
}

bool HeatModel::watched(uint8_t channel) const
{
  return channel < HEAT_CHANNELS && coil(channel).measured() && config.volts > 0 && config.limitC > 0;
}

float HeatModel::heating(float duty) const
{
  return config.squared ? duty * duty : duty;
}

float HeatModel::meanHeating(const Drive &drive, uint32_t fromMs, uint32_t toMs) const
{
  if (toMs <= fromMs)
  {
    return 0;
  }
  // Times from the drive's start.
  uint32_t from = fromMs - drive.startMs, to = toMs - drive.startMs;
  float sum = 0;
  uint32_t segmentStart = 0;
  for (uint8_t i = 0; i < drive.segments; i++)
  {
    uint32_t a = from > segmentStart ? from : segmentStart, b = to < drive.endMs[i] ? to : drive.endMs[i];
    if (b > a)
    {
      sum += heating(drive.duty[i]) * (b - a);
    }
    segmentStart = drive.endMs[i];
  }
  uint32_t holdFrom = from > segmentStart ? from : segmentStart;
  if (to > holdFrom)
  {
    sum += heating(drive.hold) * (to - holdFrom);
  }
  return sum / (to - from);
}

void HeatModel::advance(uint8_t channel, uint32_t nowMs)
{
  Channel &c = channels[channel];
  if (nowMs <= c.updatedMs)
  {
    return;
  }
  if (watched(channel))
  {
    // Piece by piece, each at one duty: towards its power x Rth, exactly.
    const CoilSettings &k = coil(channel);
    float fullWatts = config.volts * config.volts / k.ohms;
    uint32_t at = c.updatedMs;
    while (at < nowMs)
    {
      uint32_t into = at - c.drive.startMs, until = nowMs;
      for (uint8_t i = 0; i < c.drive.segments; i++)
      {
        if (into < c.drive.endMs[i])
        {
          until = c.drive.startMs + c.drive.endMs[i] < nowMs ? c.drive.startMs + c.drive.endMs[i] : nowMs;
          break;
        }
      }
      float target = fullWatts * meanHeating(c.drive, at, until) * k.rthCPerW;
      c.rise = target + (c.rise - target) * expf(-(float)(until - at) / 1000.0f / k.tauS);
      at = until;
    }
  }
  c.updatedMs = nowMs;
}

void HeatModel::update(uint32_t nowMs)
{
  for (uint8_t channel = 0; channel < HEAT_CHANNELS; channel++)
  {
    advance(channel, nowMs);
  }
}

void HeatModel::drive(uint8_t channel, const Drive &next, uint32_t nowMs)
{
  advance(channel, nowMs); // what it was doing, up to now
  channels[channel].drive = next;
  channels[channel].drive.startMs = nowMs;
}

// Reads "<number>" up to ':' or the end; false if there's none.
static bool readField(const char *&at, float &value)
{
  char *end = nullptr;
  value = strtof(at, &end);
  if (end == at)
  {
    return false;
  }
  at = *end == ':' ? end + 1 : end;
  return true;
}

void HeatModel::command(const char *command, uint32_t nowMs)
{
  if (strncmp(command, "RESET", 5) == 0)
  {
    for (uint8_t channel = 0; channel < HEAT_CHANNELS; channel++)
    {
      drive(channel, Drive(), nowMs); // the drivers restart with every output off
    }
    return;
  }
  if ((command[0] != 'P' && command[0] != 'R') || command[1] != ':')
  {
    return;
  }
  char *end = nullptr;
  long channel = strtol(command + 2, &end, 10);
  if (end == command + 2 || channel < 0 || channel >= HEAT_CHANNELS)
  {
    return;
  }
  Drive next;
  if (command[0] == 'P' && *end == ':')
  {
    // As the drivers play it: the duty for the strike time, the phases, then the hold.
    const char *at = end + 1;
    float fields[2 * 3 + 3];
    uint8_t count = 0;
    while (*at != '\0' && count < sizeof(fields) / sizeof(fields[0]) && readField(at, fields[count]))
    {
      count++;
    }
    if (count == 0)
    {
      return;
    }
    uint32_t strikeMs = count > 1 && fields[1] > 0 ? (uint32_t)fields[1] : DEFAULT_STRIKE_MS;
    next.duty[0] = fields[0] / 100;
    next.endMs[0] = strikeMs;
    next.segments = 1;
    uint8_t rest = count > 2 ? count - 2 : 0; // phase pairs, then maybe the hold
    for (uint8_t pair = 0; pair < rest / 2; pair++)
    {
      float duty = fields[2 + 2 * pair], ms = fields[3 + 2 * pair];
      if (duty > 0 && ms > 0 && next.segments < 5)
      {
        next.duty[next.segments] = duty / 100;
        next.endMs[next.segments] = next.endMs[next.segments - 1] + (uint32_t)ms;
        next.segments++;
      }
    }
    next.hold = rest % 2 == 1 ? fields[count - 1] / 100 : DEFAULT_HOLD;
  }
  drive((uint8_t)channel, next, nowMs); // R: off
}

bool HeatModel::on(uint8_t channel, uint32_t nowMs) const
{
  if (channel >= HEAT_CHANNELS)
  {
    return false;
  }
  const Drive &d = channels[channel].drive;
  uint32_t into = nowMs - d.startMs;
  for (uint8_t i = 0; i < d.segments; i++)
  {
    if (into < d.endMs[i])
    {
      return d.duty[i] > 0;
    }
  }
  return d.hold > 0;
}

float HeatModel::riseC(uint8_t channel) const
{
  return channel < HEAT_CHANNELS ? channels[channel].rise : 0;
}

float HeatModel::load(uint8_t channel) const
{
  return watched(channel) ? channels[channel].rise / config.limitC : 0;
}

uint8_t HeatModel::hottest() const
{
  uint8_t hottest = HEAT_CHANNELS;
  for (uint8_t channel = 0; channel < HEAT_CHANNELS; channel++)
  {
    if (watched(channel) && (hottest == HEAT_CHANNELS || load(channel) > load(hottest)))
    {
      hottest = channel;
    }
  }
  return hottest;
}

float HeatModel::secondsToCool(uint8_t channel, float targetLoad) const
{
  if (!watched(channel) || load(channel) <= targetLoad || targetLoad <= 0)
  {
    return 0;
  }
  return coil(channel).tauS * logf(load(channel) / targetLoad);
}
