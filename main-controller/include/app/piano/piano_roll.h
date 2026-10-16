#ifndef PIANO_ROLL_H
#define PIANO_ROLL_H

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
  uint16_t volume = 100;     // percent the velocities were scaled by
  uint32_t leadMs = 0;       // everything plays this much after the MIDI, so presses can go early
  // The MIDI it was built from, to build it again at another volume.
  std::shared_ptr<const std::vector<MidiEvent>> source;
};

#endif // PIANO_ROLL_H
