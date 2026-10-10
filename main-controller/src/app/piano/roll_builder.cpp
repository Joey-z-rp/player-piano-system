#include "app/piano/roll_builder.h"
#include <algorithm>
#include <cstdint>
#include <new>

namespace
{
const uint8_t LOWEST_PIANO_NOTE = 21; // A0
const uint32_t MIN_NOTE_MS = 40;      // shortest press that sounds
const uint32_t RESTRIKE_GAP_MS = 80;  // release this long before striking the same key again
const uint32_t PEDAL_MIN_MS = 100;    // shortest time between sustain pedal changes

bool isPianoNote(uint8_t note)
{
  return note >= LOWEST_PIANO_NOTE && note < LOWEST_PIANO_NOTE + PIANO_KEY_COUNT;
}

struct Note
{
  uint32_t onMs;
  uint32_t offMs;
  uint8_t key;
  uint8_t velocity; // 0 marks a note merged into an earlier one
};

// Pairs note-ons with note-offs and picks out sustain changes. A note-on
// for a key that's still down ends the sounding note there; the new note
// then lasts until every overlapping note on that key has ended.
void collectNotesAndPedal(const std::vector<MidiEvent> &events, std::vector<Note> &notes,
                          std::vector<RollEvent> &pedal, PianoRoll &roll)
{
  const size_t NO_NOTE = SIZE_MAX;
  size_t soundingNote[PIANO_KEY_COUNT];
  std::fill(soundingNote, soundingNote + PIANO_KEY_COUNT, NO_NOTE);
  uint8_t heldCount[PIANO_KEY_COUNT] = {};
  bool sustain = false;

  for (const MidiEvent &event : events)
  {
    switch (event.type)
    {
    case MidiEventType::NoteOn:
    {
      if (!isPianoNote(event.note))
      {
        roll.skippedNotes++;
        break;
      }
      uint8_t key = event.note - LOWEST_PIANO_NOTE;
      if (soundingNote[key] != NO_NOTE)
      {
        notes[soundingNote[key]].offMs = event.timeMs;
      }
      notes.push_back({event.timeMs, UINT32_MAX, key, event.value});
      soundingNote[key] = notes.size() - 1;
      if (heldCount[key] < 255)
      {
        heldCount[key]++;
      }
      break;
    }

    case MidiEventType::NoteOff:
    {
      if (!isPianoNote(event.note))
      {
        break;
      }
      uint8_t key = event.note - LOWEST_PIANO_NOTE;
      if (heldCount[key] == 0)
      {
        break; // note-off without a matching note-on
      }
      if (--heldCount[key] == 0)
      {
        notes[soundingNote[key]].offMs = event.timeMs;
        soundingNote[key] = NO_NOTE;
      }
      break;
    }

    case MidiEventType::Sustain:
    {
      bool on = event.value >= 64;
      if (on != sustain)
      {
        sustain = on;
        pedal.push_back({event.timeMs, on ? RollEventType::SustainOn : RollEventType::SustainOff, 0, 0});
      }
      break;
    }
    }
  }

  // Notes never turned off end with the file.
  uint32_t endMs = events.empty() ? 0 : events.back().timeMs;
  for (Note &note : notes)
  {
    if (note.offMs == UINT32_MAX)
    {
      note.offMs = endMs;
    }
  }
}

// A key can only strike again after it has been held for MIN_NOTE_MS and
// then released for RESTRIKE_GAP_MS, so the hammer resets. A repeat that
// comes sooner can't be a new strike, so it's merged into the one before it:
// the key stays down until the later note ends, at the louder velocity. A
// fast run of repeats keeps its first onset and plays as many strikes as the
// key can manage; a grace note followed by a long note becomes one long note.
void separateRepeats(std::vector<Note> &notes, PianoRoll &roll)
{
  std::stable_sort(notes.begin(), notes.end(), [](const Note &a, const Note &b) {
    if (a.key != b.key)
    {
      return a.key < b.key;
    }
    return a.onMs < b.onMs;
  });

  Note *kept = nullptr;
  for (Note &note : notes)
  {
    note.offMs = std::max(note.offMs, note.onMs + MIN_NOTE_MS);
    if (kept != nullptr && kept->key == note.key)
    {
      if (note.onMs < kept->onMs + MIN_NOTE_MS + RESTRIKE_GAP_MS)
      {
        kept->offMs = std::max(kept->offMs, note.offMs);
        kept->velocity = std::max(kept->velocity, note.velocity);
        note.velocity = 0;
        roll.mergedNotes++;
        continue;
      }
      kept->offMs = std::min(kept->offMs, note.onMs - RESTRIKE_GAP_MS);
    }
    kept = &note;
  }
}

// The pedal solenoids need PEDAL_MIN_MS between changes to finish moving.
// Changes are checked against the last one kept, so delays never pile up:
// - release then a quick re-press (a pedal change): the release stays on
//   time, so the old harmony is cleared, and the re-press waits.
// - press then a quick release: too brief to matter, so both are dropped.
// pedal alternates on/off, starting with on.
void spacePedal(const std::vector<RollEvent> &pedal, PianoRoll &roll)
{
  std::vector<RollEvent> kept;
  for (RollEvent change : pedal)
  {
    if (!kept.empty() && change.timeMs < kept.back().timeMs + PEDAL_MIN_MS)
    {
      if (kept.back().type == RollEventType::SustainOn)
      {
        kept.pop_back();
        continue;
      }
      change.timeMs = kept.back().timeMs + PEDAL_MIN_MS;
    }
    kept.push_back(change);
  }
  roll.events.insert(roll.events.end(), kept.begin(), kept.end());
}

// At the same moment: release keys, then move the pedal, then press keys.
uint8_t eventOrder(RollEventType type)
{
  switch (type)
  {
  case RollEventType::KeyRelease:
    return 0;
  case RollEventType::SustainOn:
  case RollEventType::SustainOff:
    return 1;
  case RollEventType::KeyPress:
    return 2;
  }
  return 2;
}
} // namespace

bool buildPianoRoll(const std::vector<MidiEvent> &events, PianoRoll &roll)
{
  roll.events.clear();
  roll.durationMs = 0;
  roll.noteCount = 0;
  roll.skippedNotes = 0;
  roll.mergedNotes = 0;

  try
  {
    std::vector<Note> notes;
    std::vector<RollEvent> pedal;
    collectNotesAndPedal(events, notes, pedal, roll);
    separateRepeats(notes, roll);
    spacePedal(pedal, roll);

    for (const Note &note : notes)
    {
      if (note.velocity == 0)
      {
        continue;
      }
      roll.events.push_back({note.onMs, RollEventType::KeyPress, note.key, note.velocity});
      roll.events.push_back({note.offMs, RollEventType::KeyRelease, note.key, 0});
      roll.noteCount++;
    }

    std::stable_sort(roll.events.begin(), roll.events.end(), [](const RollEvent &a, const RollEvent &b) {
      if (a.timeMs != b.timeMs)
      {
        return a.timeMs < b.timeMs;
      }
      return eventOrder(a.type) < eventOrder(b.type);
    });

    roll.events.shrink_to_fit();
    roll.durationMs = roll.events.empty() ? 0 : roll.events.back().timeMs;
    return true;
  }
  catch (const std::bad_alloc &)
  {
    roll.events.clear();
    return false;
  }
}
