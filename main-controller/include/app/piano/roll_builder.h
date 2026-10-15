#ifndef ROLL_BUILDER_H
#define ROLL_BUILDER_H

#include "app/midi/midi_parser.h"
#include "app/piano/piano_roll.h"

class VelocityTable;

// Arranges a MIDI timeline for this piano: notes map onto the 88 keys
// (others are counted as skipped), and the timing is adjusted for the
// solenoids. Repeated notes on a key are separated so it's released before
// it strikes again, with repeats too fast for that held as one note (see
// separateRepeats). Sustain pedal changes are spaced so the pedal solenoids
// can keep up (see spacePedal). With a velocity table that has landing times,
// each press is sent early by its hammer's landing time, so soft and loud
// notes sound when the MIDI says (see allowForLanding). Returns false if it
// runs out of memory.
bool buildPianoRoll(const std::vector<MidiEvent> &events, PianoRoll &roll, const VelocityTable *timing = nullptr);

#endif // ROLL_BUILDER_H
