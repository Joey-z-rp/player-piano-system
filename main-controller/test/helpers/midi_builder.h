#ifndef MIDI_BUILDER_H
#define MIDI_BUILDER_H

// Builds Standard MIDI Files in memory for tests, so each test can state
// the events it needs instead of carrying opaque byte arrays.

#include <stdint.h>
#include <initializer_list>
#include <utility>
#include <vector>

typedef std::vector<uint8_t> Bytes;

inline void appendVarLen(Bytes &out, uint32_t value)
{
  uint8_t groups[4];
  int count = 0;
  do
  {
    groups[count++] = value & 0x7F;
    value >>= 7;
  } while (value != 0 && count < 4);
  for (int i = count - 1; i >= 0; i--)
  {
    out.push_back(groups[i] | (i > 0 ? 0x80 : 0));
  }
}

inline void appendU16(Bytes &out, uint16_t value)
{
  out.push_back(value >> 8);
  out.push_back(value & 0xFF);
}

inline void appendU32(Bytes &out, uint32_t value)
{
  appendU16(out, value >> 16);
  appendU16(out, value & 0xFFFF);
}

// Events are added at absolute ticks, in order; delta times are worked out.
class TrackBuilder
{
public:
  TrackBuilder &at(uint32_t tick, std::initializer_list<uint8_t> event)
  {
    appendVarLen(body, tick - lastTick);
    body.insert(body.end(), event.begin(), event.end());
    lastTick = tick;
    return *this;
  }

  TrackBuilder &noteOn(uint32_t tick, uint8_t note, uint8_t velocity, uint8_t channel = 0)
  {
    return at(tick, {(uint8_t)(0x90 | channel), note, velocity});
  }

  TrackBuilder &noteOff(uint32_t tick, uint8_t note, uint8_t channel = 0)
  {
    return at(tick, {(uint8_t)(0x80 | channel), note, 0});
  }

  TrackBuilder &sustain(uint32_t tick, uint8_t value, uint8_t channel = 0)
  {
    return at(tick, {(uint8_t)(0xB0 | channel), 64, value});
  }

  TrackBuilder &tempo(uint32_t tick, uint32_t usPerQuarter)
  {
    return at(tick, {0xFF, 0x51, 0x03, (uint8_t)(usPerQuarter >> 16), (uint8_t)(usPerQuarter >> 8),
                     (uint8_t)usPerQuarter});
  }

  Bytes build(bool endOfTrack = true) const
  {
    Bytes events = body;
    if (endOfTrack)
    {
      events.insert(events.end(), {0x00, 0xFF, 0x2F, 0x00});
    }
    Bytes chunk = {'M', 'T', 'r', 'k'};
    appendU32(chunk, events.size());
    chunk.insert(chunk.end(), events.begin(), events.end());
    return chunk;
  }

private:
  Bytes body;
  uint32_t lastTick = 0;
};

inline Bytes midiFile(uint16_t format, uint16_t division, std::initializer_list<Bytes> tracks)
{
  Bytes file = {'M', 'T', 'h', 'd'};
  appendU32(file, 6);
  appendU16(file, format);
  appendU16(file, tracks.size());
  appendU16(file, division);
  for (const Bytes &track : tracks)
  {
    file.insert(file.end(), track.begin(), track.end());
  }
  return file;
}

// 25 fps x 40 ticks per frame = 1000 ticks per second, so ticks are ms.
const uint16_t MS_DIVISION = (uint16_t)((0x100 - 25) << 8 | 40);

#endif // MIDI_BUILDER_H
