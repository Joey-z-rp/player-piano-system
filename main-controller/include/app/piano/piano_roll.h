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
  bool pedal : 1;   // KeyPress only: the sustain pedal is down, so it plays as calibrated with it down
  bool repeat : 1;  // KeyPress only: struck again soon after the last on its key, so clamped as a repeat
  // KeyPress only, worked out when the roll is built so playing only formats
  // the command (VelocityTable::strikeCommand): the duty to strike with, in
  // hundredths of a percent, and ms from the press until the hammer lands
  // (0 if unknown), the key's adjustments and all.
  uint16_t duty;
  uint16_t landingMs;
};

// How a piece's velocities are shaped before it's played, in this order:
// each part's offset, voicing, contrast, then volume (see buildPianoRoll);
// then repeats are boosted (see separateRepeats in roll_builder.cpp).
struct Dynamics
{
  uint16_t volume = 100;   // percent: every velocity times this
  uint16_t contrast = 100; // percent: velocities stretched away from the piece's median (100 = as written)
  uint16_t voicing = 100;  // percent: within each chord, velocities stretched away from the chord's average
  int8_t partOffset[MAX_MIDI_PARTS] = {}; // velocities added to each part's notes (see MidiEvent::part)
  uint8_t repeatBoost = 0; // velocities added to a repeat, so it can strike again sooner (and isn't quieter)
  // Percent of the written tempo: the MIDI's times are scaled by 100/speed
  // before anything else, so the piano's own limits (repeats, landing, the
  // pedal) stay in real ms.
  uint16_t speed = 100;
  // Parts left out (bit n: MidiEvent::part n), as if the file didn't have
  // them: they don't sound, and don't shape or crowd the rest.
  uint16_t mutedParts = 0;

  bool operator==(const Dynamics &other) const
  {
    return volume == other.volume && contrast == other.contrast && voicing == other.voicing &&
           repeatBoost == other.repeatBoost && speed == other.speed &&
           mutedParts == other.mutedParts && std::equal(partOffset, partOffset + MAX_MIDI_PARTS, other.partOffset);
  }
  bool operator!=(const Dynamics &other) const
  {
    return !(*this == other);
  }
};

struct NoteEdits;

struct PianoRoll
{
  std::string title;
  uint32_t id = 0;               // the upload it came from: kept when it's rebuilt
  std::string libraryId;         // its PieceInfo::id in the library, if it's saved there
  std::vector<RollEvent> events; // sorted by timeMs
  uint32_t durationMs = 0;
  uint32_t noteCount = 0;
  uint32_t skippedNotes = 0; // outside the 88-key range
  uint32_t mergedNotes = 0;  // repeats too soon to re-strike, held as one note
  uint32_t duplicateNotes = 0; // the same note twice at once in the file, played once
  Dynamics dynamics;         // how the velocities were shaped
  uint32_t configVersion = 0; // the PlayConfig it was built with (PianoController::playConfigVersion)
  uint32_t leadMs = 0;       // everything plays this much after the MIDI, so presses can go early
  // The MIDI it was built from, to build it again with other dynamics, and its parts' names.
  std::shared_ptr<const std::vector<MidiEvent>> source;
  std::vector<std::string> partNames;
  std::shared_ptr<const NoteEdits> edits; // the note edits it was built with (null: none)
};

#endif // PIANO_ROLL_H
