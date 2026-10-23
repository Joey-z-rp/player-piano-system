#ifndef NOTE_EDITS_H
#define NOTE_EDITS_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

// Touch-ups to a piece, made on the play page's note view: a note played
// louder or softer, held longer or shorter, or moved earlier or later; a
// sustain pedal press put down or lifted earlier or later, or left out. A
// note is the one on its key whose note-on is at atMs in the MIDI (as
// written, at its own tempo), a pedal press the one going down at atMs, so
// edits stay with them at any speed and dynamics. Saved with the piece in the
// library (PieceLibrary::saveEdits); the roll builder applies them
// (buildPianoRoll).
static const size_t MAX_NOTE_EDITS = 1000;
static const int16_t MAX_EDIT_VELOCITY = 126; // velocities added (or taken)
static const int16_t MAX_EDIT_LENGTH_MS = 5000;
static const int16_t MAX_EDIT_SHIFT_MS = 1000;
static const size_t MAX_PEDAL_EDITS = 500;
static const int16_t MAX_PEDAL_SHIFT_MS = 2000;

struct NoteEdit
{
  uint32_t atMs;    // the note-on in the MIDI, in ms as written
  uint8_t key;      // 0-87
  int8_t velocity;  // added to its written velocity, before the dynamics
  int16_t lengthMs; // added to how long it's held, in ms as written
  int16_t shiftMs;  // added to when it's played (negative: earlier), in ms as written

  bool operator==(const NoteEdit &other) const
  {
    return atMs == other.atMs && key == other.key && velocity == other.velocity && lengthMs == other.lengthMs &&
           shiftMs == other.shiftMs;
  }
};

struct PedalEdit
{
  uint32_t atMs;  // when the press goes down in the MIDI, in ms as written
  int16_t downMs; // added to when it goes down (negative: earlier), in ms as written
  int16_t upMs;   // added to when it's lifted, likewise
  bool removed;   // left out

  bool operator==(const PedalEdit &other) const
  {
    return atMs == other.atMs && downMs == other.downMs && upMs == other.upMs && removed == other.removed;
  }
};

// A piece's edits: notes in order of atMs then key, at most one per note;
// pedal presses in order of atMs, at most one per press.
struct NoteEdits
{
  std::vector<NoteEdit> edits;
  std::vector<PedalEdit> pedal;

  // The note's edit, or null.
  const NoteEdit *find(uint8_t key, uint32_t atMs) const;
  // The pedal press's edit, or null.
  const PedalEdit *findPedal(uint32_t atMs) const;
  bool empty() const
  {
    return edits.empty() && pedal.empty();
  }
  bool operator==(const NoteEdits &other) const
  {
    return edits == other.edits && pedal == other.pedal;
  }
};

// Reads {"notes": [[key, at ms, velocity, length ms, shift ms], ...],
// "pedal": [[at ms, down ms, up ms, removed (0 or 1)], ...]} (either left
// out: none). Edits that change nothing are dropped; one edited twice keeps
// the last. All or nothing: on an error edits is unchanged and error says why.
bool readNoteEdits(const char *json, size_t length, NoteEdits &edits, std::string &error);
std::string noteEditsJson(const NoteEdits &edits);

#endif // NOTE_EDITS_H
