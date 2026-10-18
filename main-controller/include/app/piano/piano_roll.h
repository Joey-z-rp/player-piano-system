#ifndef PIANO_ROLL_H
#define PIANO_ROLL_H

#include <algorithm>
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>
#include "app/midi/midi_parser.h"

static const uint8_t PIANO_KEY_COUNT = 88;

enum class RollEventType : uint8_t
{
  KeyPress,
  KeyRelease,
  SustainOn,
  SustainOff
};

struct RollEvent
{
  uint32_t timeMs; // from the start of the roll
  RollEventType type;
  uint8_t key;      // 0-87 (A0-C8), key events only
  uint8_t velocity; // 1-127, KeyPress only
  bool pedal;       // KeyPress only: the sustain pedal is down, so it plays as calibrated with it down
};

// How a piece's velocities are shaped before it's played, in this order:
// each part's offset, voicing, contrast, then volume (see buildPianoRoll).
struct Dynamics
{
  uint16_t volume = 100;   // percent: every velocity times this
  uint16_t contrast = 100; // percent: velocities stretched away from the piece's median (100 = as written)
  uint16_t voicing = 100;  // percent: within each chord, velocities stretched away from the chord's average
  int8_t partOffset[MAX_MIDI_PARTS] = {}; // velocities added to each part's notes (see MidiEvent::part)

  bool operator==(const Dynamics &other) const
  {
    return volume == other.volume && contrast == other.contrast && voicing == other.voicing &&
           std::equal(partOffset, partOffset + MAX_MIDI_PARTS, other.partOffset);
  }
  bool operator!=(const Dynamics &other) const
  {
    return !(*this == other);
  }
};

struct PianoRoll
{
  std::string title;
  std::vector<RollEvent> events; // sorted by timeMs
  uint32_t durationMs = 0;
  uint32_t noteCount = 0;
  uint32_t skippedNotes = 0; // outside the 88-key range
  uint32_t mergedNotes = 0;  // repeats too soon to re-strike, held as one note
  uint32_t duplicateNotes = 0; // the same note twice at once in the file, played once
  Dynamics dynamics;         // how the velocities were shaped
  uint32_t leadMs = 0;       // everything plays this much after the MIDI, so presses can go early
  // The MIDI it was built from, to build it again with other dynamics, and its parts' names.
  std::shared_ptr<const std::vector<MidiEvent>> source;
  std::vector<std::string> partNames;
};

#endif // PIANO_ROLL_H
