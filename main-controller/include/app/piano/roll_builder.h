#ifndef ROLL_BUILDER_H
#define ROLL_BUILDER_H

#include <bitset>
#include "app/midi/midi_parser.h"
#include "app/piano/piano_roll.h"
#include "app/piano/note_edits.h"

class VelocityTable;
struct PlayConfig;

// What became of each written note, for showing what the builder changed.
enum NoteChange : uint16_t
{
  NOTE_MERGED = 1,           // a repeat too soon to strike again (or a note doubled at another velocity): not struck
  NOTE_ABSORBED = 2,         // a strike that repeats were merged into: held longer, or louder (and re-timed), for them
  NOTE_SHORTENED = 4,        // released early, so the key can reset before its next strike
  NOTE_LENGTHENED = 8,       // held on until its hammer has landed
  NOTE_VELOCITY_LIMITED = 16, // the dynamics took it past 1-127
  NOTE_DUPLICATE = 32,        // the same note again at the same moment and velocity: played once
  NOTE_REPEAT_BOOSTED = 64,   // a repeat played louder (Dynamics::repeatBoost), so it can strike again sooner
  NOTE_MUTED = 128,           // its part is muted (Dynamics::mutedParts): not played
  NOTE_EDITED = 256           // edited by hand (NoteEdits): louder or softer, longer or shorter, or moved
};

struct NoteReport
{
  uint32_t onMs;          // as written, on the MIDI's timeline
  uint32_t writtenOffMs;  // as written
  uint32_t playedOnMs;    // when it sounds, on the same timeline; 0 if merged
  uint32_t playedOffMs;   // when the key is released, on the same timeline; 0 if merged
  uint32_t mergedIntoMs;  // merged: the onMs of the strike it's held in
  uint8_t key;            // 0-87
  uint8_t writtenVelocity;
  uint8_t playedVelocity; // after the dynamics and any merges; 0 if merged
  uint8_t part;           // see MidiEvent::part
  uint16_t changes;       // NoteChange flags
  uint32_t sourceMs;      // its note-on in the MIDI as written (at its own tempo): what NoteEdit::atMs names
};

struct PedalSpan
{
  uint32_t downMs; // on the MIDI's timeline
  uint32_t upMs;
  uint32_t sourceMs; // as written: when it goes down in the MIDI at its own tempo (what a PedalEdit names)
};

struct RollReport
{
  std::vector<NoteReport> notes; // in written order
  std::vector<PedalSpan> writtenPedal;
  std::vector<PedalSpan> playedPedal; // after spacing, on the MIDI's timeline
};

// Arranges a MIDI timeline for this piano: notes map onto the 88 keys
// (others are counted as skipped), and the timing is adjusted for the
// solenoids. Repeated notes on a key are separated so it's released before
// it strikes again, with repeats too fast for that held as one note (see
// separateRepeats). Sustain pedal changes are spaced so the pedal solenoids
// can keep up (see spacePedal). With a velocity table that has landing times,
// each press is sent early by its hammer's landing time, so soft and loud
// notes sound when the MIDI says (see allowForLanding). The velocities are
// shaped by dynamics first (see shapeVelocities; kept within 1-127). With a
// report, it also says what became of every note and the pedal. The timing
// rules come from config (the defaults without one). With edits, each edited
// note's velocity change is added before the dynamics, and its length and
// shift (scaled to the speed) before the timing rules, which then treat it
// like any other note; edited pedal presses are moved or left out before the
// pedal is spaced. Returns false if it runs out of memory.
bool buildPianoRoll(const std::vector<MidiEvent> &events, PianoRoll &roll, const VelocityTable *timing,
                    const Dynamics &dynamics, RollReport *report = nullptr, const PlayConfig *config = nullptr,
                    const NoteEdits *edits = nullptr);
// The same, with only a volume.
bool buildPianoRoll(const std::vector<MidiEvent> &events, PianoRoll &roll, const VelocityTable *timing = nullptr,
                    uint16_t volume = 100, RollReport *report = nullptr);

// Notes starting within this long of a group's first note are one chord, for voicing.
static const uint32_t CHORD_WINDOW_MS = 40;

// Where to pick up a roll part way through, so the notes that sound from
// fromMs on its timeline play. The clock starts the roll's lead earlier, so
// their presses are sent in time; presses in that lead whose notes sound
// before fromMs (by their landing times) are skipped, so resuming from a
// pause doesn't strike them twice. Notes already held aren't struck (a hammer
// can't start a note half way through), so their releases are skipped too;
// the pedal is pressed first if it's down at the start.
struct RollStart
{
  size_t index;                              // first event to play
  uint32_t timeMs;                           // the roll time the clock starts at
  bool pedalDown;                            // the pedal is down at that time
  std::bitset<PIANO_KEY_COUNT> skipReleases; // keys whose next release isn't played
  std::vector<size_t> skippedPresses;        // indexes of presses not played, in order
};
RollStart findRollStart(const PianoRoll &roll, uint32_t fromMs, const VelocityTable *timing = nullptr);

#endif // ROLL_BUILDER_H
