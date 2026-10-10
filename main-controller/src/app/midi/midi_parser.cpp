#include "app/midi/midi_parser.h"
#include "app/midi/byte_reader.h"
#include <algorithm>
#include <new>

namespace
{
const uint8_t DRUM_CHANNEL = 9; // channel 10, zero-based
const uint8_t SUSTAIN_CONTROLLER = 64;
const uint32_t DEFAULT_TEMPO_US = 500000; // 120 BPM
const uint32_t CHUNK_MTHD = 0x4D546864;
const uint32_t CHUNK_MTRK = 0x4D54726B;

// Declared in the order events sharing a tick are applied: tempo first,
// then note-offs, sustain, and note-ons, so a key is lifted before it's
// pressed again.
enum class RawKind : uint8_t
{
  Tempo,
  NoteOff,
  Sustain,
  NoteOn
};

struct RawEvent
{
  uint32_t tick;
  uint32_t value; // tempo (us per quarter), velocity, or controller value
  RawKind kind;
  uint8_t note;
};

bool parseTrack(ByteReader track, std::vector<RawEvent> &raw, std::string &error)
{
  uint32_t tick = 0;
  uint8_t runningStatus = 0;

  while (!track.atEnd())
  {
    uint32_t delta;
    uint8_t status;
    if (!track.varLen(delta) || !track.peek(status))
    {
      error = "truncated track";
      return false;
    }
    tick += delta;

    if (status & 0x80)
    {
      track.skip(1);
    }
    else if (runningStatus != 0)
    {
      status = runningStatus;
    }
    else
    {
      error = "data byte without a status byte";
      return false;
    }

    if (status == 0xFF)
    {
      uint8_t metaType;
      uint32_t metaLength;
      if (!track.u8(metaType) || !track.varLen(metaLength))
      {
        error = "truncated meta event";
        return false;
      }
      runningStatus = 0;
      if (metaType == 0x2F)
      {
        return true; // End of Track
      }
      if (metaType == 0x51 && metaLength == 3)
      {
        uint8_t b0, b1, b2;
        if (!track.u8(b0) || !track.u8(b1) || !track.u8(b2))
        {
          error = "truncated tempo event";
          return false;
        }
        raw.push_back({tick, ((uint32_t)b0 << 16) | ((uint32_t)b1 << 8) | b2, RawKind::Tempo, 0});
      }
      else if (!track.skip(metaLength))
      {
        error = "truncated meta event";
        return false;
      }
      continue;
    }

    if (status == 0xF0 || status == 0xF7)
    {
      uint32_t sysexLength;
      if (!track.varLen(sysexLength) || !track.skip(sysexLength))
      {
        error = "truncated sysex event";
        return false;
      }
      runningStatus = 0;
      continue;
    }

    if (status > 0xF0)
    {
      error = "unexpected system message";
      return false;
    }

    runningStatus = status;
    uint8_t type = status & 0xF0;
    uint8_t channel = status & 0x0F;
    uint8_t data1, data2 = 0;
    bool oneDataByte = (type == 0xC0 || type == 0xD0);
    if (!track.u8(data1) || (!oneDataByte && !track.u8(data2)))
    {
      error = "truncated channel event";
      return false;
    }

    if (channel == DRUM_CHANNEL)
    {
      continue;
    }
    if (type == 0x90 && data2 > 0)
    {
      raw.push_back({tick, data2, RawKind::NoteOn, data1});
    }
    else if (type == 0x80 || type == 0x90)
    {
      raw.push_back({tick, 0, RawKind::NoteOff, data1});
    }
    else if (type == 0xB0 && data1 == SUSTAIN_CONTROLLER)
    {
      raw.push_back({tick, data2, RawKind::Sustain, 0});
    }
  }

  // Missing End of Track; keep what was read.
  return true;
}

bool readTracks(ByteReader file, uint16_t &division, std::vector<RawEvent> &raw, std::string &error)
{
  uint32_t chunkId, chunkLength;
  uint16_t format, trackCount;
  if (!file.u32(chunkId) || chunkId != CHUNK_MTHD || !file.u32(chunkLength) || chunkLength < 6)
  {
    error = "not a MIDI file";
    return false;
  }
  if (!file.u16(format) || !file.u16(trackCount) || !file.u16(division) || !file.skip(chunkLength - 6))
  {
    error = "truncated header";
    return false;
  }
  if (format > 1)
  {
    error = "MIDI format 2 is not supported";
    return false;
  }
  if (division & 0x8000)
  {
    int framesPerSecond = -(int8_t)(division >> 8);
    bool knownRate = framesPerSecond == 24 || framesPerSecond == 25 || framesPerSecond == 29 || framesPerSecond == 30;
    if (!knownRate || (division & 0xFF) == 0)
    {
      error = "invalid time division";
      return false;
    }
  }
  else if (division == 0)
  {
    error = "invalid time division";
    return false;
  }

  uint16_t tracksRead = 0;
  while (!file.atEnd() && tracksRead < trackCount)
  {
    ByteReader chunk(nullptr, 0);
    if (!file.u32(chunkId) || !file.u32(chunkLength) || !file.sub(chunkLength, chunk))
    {
      error = "truncated file";
      return false;
    }
    if (chunkId != CHUNK_MTRK)
    {
      continue; // unknown chunks are skipped, per the spec
    }
    if (!parseTrack(chunk, raw, error))
    {
      error = "track " + std::to_string(tracksRead) + ": " + error;
      return false;
    }
    tracksRead++;
  }

  if (tracksRead == 0)
  {
    error = "no tracks";
    return false;
  }
  return true;
}

// Converts ticks to microseconds. Each tempo change starts a new segment,
// and times are computed from the segment start, so rounding never builds up.
class TickClock
{
public:
  explicit TickClock(uint16_t division) : segmentTick(0), segmentUs(0)
  {
    if (division & 0x8000)
    {
      // SMPTE: frames per second (negative) in the high byte, ticks per frame in the low byte.
      int framesPerSecond = -(int8_t)(division >> 8);
      uint32_t ticksPerFrame = division & 0xFF;
      fixedRate = true;
      if (framesPerSecond == 29)
      {
        // 29.97 drop-frame
        usPerUnit = 100000000ULL;
        ticksPerUnit = 2997 * ticksPerFrame;
      }
      else
      {
        usPerUnit = 1000000ULL;
        ticksPerUnit = framesPerSecond * ticksPerFrame;
      }
    }
    else
    {
      fixedRate = false;
      usPerUnit = DEFAULT_TEMPO_US;
      ticksPerUnit = division;
    }
  }

  uint64_t toUs(uint32_t tick) const
  {
    return segmentUs + (uint64_t)(tick - segmentTick) * usPerUnit / ticksPerUnit;
  }

  void setTempo(uint32_t tick, uint32_t tempoUs)
  {
    if (fixedRate || tempoUs == 0)
    {
      return;
    }
    segmentUs = toUs(tick);
    segmentTick = tick;
    usPerUnit = tempoUs;
  }

private:
  bool fixedRate;
  uint64_t usPerUnit;
  uint32_t ticksPerUnit;
  uint32_t segmentTick;
  uint64_t segmentUs;
};

// Merges all tracks into one timeline and converts ticks to milliseconds.
void toTimeline(uint16_t division, std::vector<RawEvent> &raw, std::vector<MidiEvent> &events)
{
  std::stable_sort(raw.begin(), raw.end(), [](const RawEvent &a, const RawEvent &b) {
    if (a.tick != b.tick)
    {
      return a.tick < b.tick;
    }
    return a.kind < b.kind;
  });

  TickClock clock(division);
  events.reserve(raw.size());
  for (const RawEvent &event : raw)
  {
    uint32_t timeMs = (uint32_t)(clock.toUs(event.tick) / 1000);
    switch (event.kind)
    {
    case RawKind::Tempo:
      clock.setTempo(event.tick, event.value);
      break;
    case RawKind::NoteOff:
      events.push_back({timeMs, MidiEventType::NoteOff, event.note, 0});
      break;
    case RawKind::Sustain:
      events.push_back({timeMs, MidiEventType::Sustain, 0, (uint8_t)event.value});
      break;
    case RawKind::NoteOn:
      events.push_back({timeMs, MidiEventType::NoteOn, event.note, (uint8_t)event.value});
      break;
    }
  }
}
} // namespace

bool parseMidiFile(const uint8_t *data, size_t length, std::vector<MidiEvent> &events, std::string &error)
{
  events.clear();
  if (data == nullptr && length > 0)
  {
    error = "no data";
    return false;
  }

  try
  {
    uint16_t division = 0;
    std::vector<RawEvent> raw;
    if (!readTracks(ByteReader(data, length), division, raw, error))
    {
      return false;
    }
    toTimeline(division, raw, events);
    return true;
  }
  catch (const std::bad_alloc &)
  {
    events.clear();
    error = "out of memory";
    return false;
  }
}
