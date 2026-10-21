// Shared by the roll builder's files (roll_builder.cpp, roll_repeats.cpp):
// the notes it works on, and the repeat handling roll_repeats.cpp does.
#ifndef ROLL_NOTES_H
#define ROLL_NOTES_H

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "app/piano/play_config.h"
#include "app/piano/piano_roll.h"

class VelocityTable;

namespace roll_notes
{
  struct Note
  {
    uint32_t onMs;
    uint32_t offMs;
    uint8_t key;
    uint8_t velocity;   // 0 marks a note merged into an earlier one
    uint16_t landingMs; // from the press until the hammer lands; 0 if unknown
    // As written, for the report.
    uint32_t writtenOnMs;
    uint32_t writtenOffMs;
    uint8_t writtenVelocity;
    uint8_t changes;       // NoteChange flags
    uint32_t mergedIntoMs; // merged notes: when the strike they're held in is written
    bool pedal;            // the sustain pedal is down when it's written to sound
    uint8_t part;          // see MidiEvent::part
    bool repeat;           // struck within PlayConfig::repeatWindowMs of the last strike on its key
  };

  // A note-on at the same moment as another on its key, played as that one.
  struct Doubled
  {
    Note note;
    size_t original; // index in notes
  };

  // Separates the repeats (see roll_repeats.cpp).
  void separateRepeats(std::vector<Note> &notes, PianoRoll &roll, const VelocityTable *timing, uint8_t boost,
                       const PlayConfig &config);
} // namespace roll_notes

#endif // ROLL_NOTES_H
