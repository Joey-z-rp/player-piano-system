#include "app/piano/roll_builder.h"
#include "app/piano/velocity_table.h"
#include <algorithm>
#include <cstdint>
#include <new>

namespace
{
  const uint8_t LOWEST_PIANO_NOTE = 21; // A0
  const uint32_t MIN_NOTE_MS = 10;      // shortest a key is held after its hammer lands
  const uint32_t RESTRIKE_GAP_MS = 70;  // release this long before striking the same key again
  const uint32_t PEDAL_MIN_MS = 100;    // shortest time between sustain pedal changes
  // How long the pedal takes to lift the dampers fully after it's sent down
  // (its press is 60 ms at full power), before a key counts as pedal-down.
  const uint32_t PEDAL_SETTLE_MS = 100;

  bool isPianoNote(uint8_t note)
  {
    return note >= LOWEST_PIANO_NOTE && note < LOWEST_PIANO_NOTE + PIANO_KEY_COUNT;
  }

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
  };

  // A note-on at the same moment as another on its key, played as that one.
  struct Doubled
  {
    Note note;
    size_t original; // index in notes
  };

  // Pairs note-ons with note-offs and picks out sustain changes. A note-on
  // for a key that's still down ends the sounding note there; the new note
  // then lasts until every overlapping note on that key has ended. A note-on
  // at the same moment as the sounding note (two voices sharing it, or a
  // duplicate in the file) is one strike, at the louder velocity; it goes in
  // doubled, for the report.
  void collectNotesAndPedal(const std::vector<MidiEvent> &events, std::vector<Note> &notes,
                            std::vector<RollEvent> &pedal, std::vector<Doubled> &doubled, PianoRoll &roll)
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
        if (soundingNote[key] != NO_NOTE && notes[soundingNote[key]].writtenOnMs == event.timeMs)
        {
          Note &first = notes[soundingNote[key]];
          Note copy = {event.timeMs, UINT32_MAX, key, 0, 0, event.timeMs, 0, event.value, 0, event.timeMs, false};
          if (event.value == first.writtenVelocity)
          {
            copy.changes = NOTE_DUPLICATE;
            roll.duplicateNotes++;
          }
          else
          {
            copy.changes = NOTE_MERGED;
            roll.mergedNotes++;
            if (event.value > first.velocity)
            {
              first.velocity = event.value;
              first.changes |= NOTE_ABSORBED;
            }
          }
          doubled.push_back({copy, soundingNote[key]});
          if (heldCount[key] < 255)
          {
            heldCount[key]++;
          }
          break;
        }
        if (soundingNote[key] != NO_NOTE)
        {
          notes[soundingNote[key]].offMs = event.timeMs;
        }
        notes.push_back({event.timeMs, UINT32_MAX, key, event.value, 0, event.timeMs, 0, event.value, 0, 0, false});
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
          pedal.push_back({event.timeMs, on ? RollEventType::SustainOn : RollEventType::SustainOff, 0, 0, false});
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
      note.writtenOffMs = note.offMs;
    }
    for (Doubled &copy : doubled)
    {
      copy.note.writtenOffMs = notes[copy.original].writtenOffMs;
    }
  }

  // Which notes play as pedal-down, from the velocity table's pedal entries
  // (see VelocityTable): only those the dampers are surely up for through
  // the key's whole travel. The pedal, as played, went down PEDAL_SETTLE_MS
  // before the earliest the key could be pressed (its pedal-up press), and
  // doesn't start lifting before the note sounds. In any doubt a note is
  // pedal-up: a pedal-down duty played with the dampers still on the strings,
  // or falling back, may not sound at all, while a pedal-up one at worst plays
  // a little loud. Times as written, before the lead.
  void markPedal(std::vector<Note> &notes, const std::vector<RollEvent> &pedal, const VelocityTable &timing)
  {
    for (Note &note : notes)
    {
      int64_t settledBy = (int64_t)note.onMs - timing.landingMs(note.key, note.velocity) - PEDAL_SETTLE_MS;
      auto next = std::find_if(pedal.begin(), pedal.end(),
                               [settledBy](const RollEvent &change) { return (int64_t)change.timeMs > settledBy; });
      note.pedal = next != pedal.begin() && (next - 1)->type == RollEventType::SustainOn &&
                   (next == pedal.end() || next->timeMs >= note.onMs);
    }
  }

  // The hammer lands well after the press: ~55 ms at full velocity, ~150 ms at
  // the softest. Everything is played a fixed lead later, and each press is sent
  // earlier by its landing time, so every note sounds lead ms after the MIDI
  // says and soft and loud notes stay together. Releases and the pedal just move
  // by the lead.
  void allowForLanding(std::vector<Note> &notes, std::vector<RollEvent> &pedal, const VelocityTable &timing)
  {
    uint32_t lead = timing.maxLandingMs();
    for (Note &note : notes)
    {
      note.landingMs = timing.landingMs(note.key, note.velocity, note.pedal);
      note.onMs += lead - note.landingMs;
      note.offMs += lead;
    }
    for (RollEvent &change : pedal)
    {
      change.timeMs += lead;
    }
  }

  // The volume: every velocity times volume percent, kept within 1-127, so the
  // piece's dynamics stay in proportion. Before landing times and repeats are
  // worked out, which depend on velocity.
  void scaleVelocities(std::vector<Note> &notes, uint16_t volume)
  {
    for (Note &note : notes)
    {
      uint32_t scaled = ((uint32_t)note.velocity * volume + 50) / 100;
      note.velocity = (uint8_t)std::min<uint32_t>(std::max<uint32_t>(scaled, 1), 127);
      if (note.velocity != scaled)
      {
        note.changes |= NOTE_VELOCITY_LIMITED;
      }
    }
  }

  // Holds note in the strike kept until it ends. If note is louder, kept is
  // struck at its velocity instead, and so re-timed for that hammer's landing
  // to sound when the earlier of the two should. A louder hammer is faster, so
  // that's no earlier than kept was pressed; with a table where it isn't, it's
  // kept late enough for the strike before to be released and reset first.
  void mergeRepeat(Note &kept, Note &note, Note *before)
  {
    if (note.velocity > kept.velocity)
    {
      uint32_t soundsMs = std::min(kept.onMs + kept.landingMs, note.onMs + note.landingMs);
      uint32_t onMs = soundsMs - note.landingMs;
      if (before != nullptr)
      {
        onMs = std::max(onMs, before->onMs + before->landingMs + MIN_NOTE_MS + RESTRIKE_GAP_MS);
        before->offMs = std::min(before->offMs, onMs - RESTRIKE_GAP_MS);
      }
      kept.onMs = onMs;
      kept.velocity = note.velocity;
      kept.landingMs = note.landingMs;
      kept.offMs = std::max(kept.offMs, kept.onMs + kept.landingMs + MIN_NOTE_MS);
    }
    kept.offMs = std::max(kept.offMs, note.offMs);
    kept.changes |= NOTE_ABSORBED;
    note.changes |= NOTE_MERGED;
    note.mergedIntoMs = kept.writtenOnMs;
    note.velocity = 0;
  }

  // A key can only strike again after it has been held until MIN_NOTE_MS past
  // its hammer landing and then released for RESTRIKE_GAP_MS, so the hammer
  // resets. A repeat that comes sooner can't be a new strike, so it's merged
  // into the one before it: the key stays down until the later note ends, at
  // the louder velocity, sounding when the first of them should. A fast run of
  // repeats keeps its first onset and plays as many strikes as the key can
  // manage; a grace note followed by a long note becomes one long note.
  // Notes come in press order, so with landing times a soft note can come
  // before a louder one written a little earlier (or at the same time, as when
  // two voices share a note).
  void separateRepeats(std::vector<Note> &notes, PianoRoll &roll)
  {
    std::stable_sort(notes.begin(), notes.end(), [](const Note &a, const Note &b)
                     {
    if (a.key != b.key)
    {
      return a.key < b.key;
    }
    return a.onMs < b.onMs; });

    Note *kept = nullptr;
    Note *before = nullptr; // the strike before kept on its key
    for (Note &note : notes)
    {
      note.offMs = std::max(note.offMs, note.onMs + note.landingMs + MIN_NOTE_MS);
      if (kept != nullptr && kept->key == note.key)
      {
        if (note.onMs < kept->onMs + kept->landingMs + MIN_NOTE_MS + RESTRIKE_GAP_MS)
        {
          mergeRepeat(*kept, note, before);
          roll.mergedNotes++;
          continue;
        }
        kept->offMs = std::min(kept->offMs, note.onMs - RESTRIKE_GAP_MS);
        before = kept;
      }
      else
      {
        before = nullptr;
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
  std::vector<RollEvent> spacePedal(const std::vector<RollEvent> &pedal)
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
    return kept;
  }

  // Pedal changes (alternating, starting with on) as spans, moved back by lead.
  void pedalSpans(const std::vector<RollEvent> &changes, uint32_t lead, uint32_t endMs, std::vector<PedalSpan> &spans)
  {
    for (const RollEvent &change : changes)
    {
      uint32_t timeMs = change.timeMs - lead;
      if (change.type == RollEventType::SustainOn)
      {
        spans.push_back({timeMs, endMs});
      }
      else if (!spans.empty())
      {
        spans.back().upMs = timeMs;
      }
    }
  }

  // Lengths are compared on the MIDI's timeline: a played note is released
  // lead after the MIDI, like everything else.
  void reportNotes(const std::vector<Note> &notes, const std::vector<Doubled> &doubled, uint32_t lead,
                   RollReport &report)
  {
    report.notes.reserve(notes.size() + doubled.size());
    for (const Note &note : notes)
    {
      NoteReport out = {note.writtenOnMs, note.writtenOffMs, 0, 0, note.mergedIntoMs, note.key, note.writtenVelocity,
                        note.velocity, note.changes};
      if (note.velocity != 0)
      {
        out.playedOnMs = note.onMs + note.landingMs - lead;
        out.playedOffMs = note.offMs - lead;
        if (out.playedOffMs < out.writtenOffMs)
        {
          out.changes |= NOTE_SHORTENED;
        }
        else if (out.playedOffMs > out.writtenOffMs && !(note.changes & NOTE_ABSORBED))
        {
          out.changes |= NOTE_LENGTHENED;
        }
      }
      report.notes.push_back(out);
    }
    for (const Doubled &copy : doubled)
    {
      const Note &note = copy.note;
      report.notes.push_back({note.writtenOnMs, note.writtenOffMs, 0, 0, note.mergedIntoMs, note.key,
                              note.writtenVelocity, 0, note.changes});
    }
    std::stable_sort(report.notes.begin(), report.notes.end(),
                     [](const NoteReport &a, const NoteReport &b)
                     { return a.onMs < b.onMs; });
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

bool buildPianoRoll(const std::vector<MidiEvent> &events, PianoRoll &roll, const VelocityTable *timing,
                    uint16_t volume, RollReport *report)
{
  if (report != nullptr)
  {
    *report = RollReport();
  }
  roll.events.clear();
  roll.durationMs = 0;
  roll.noteCount = 0;
  roll.skippedNotes = 0;
  roll.mergedNotes = 0;
  roll.duplicateNotes = 0;
  roll.volume = volume;
  roll.leadMs = 0;

  try
  {
    std::vector<Note> notes;
    std::vector<RollEvent> pedal;
    std::vector<Doubled> doubled;
    collectNotesAndPedal(events, notes, pedal, doubled, roll);
    uint32_t endMs = events.empty() ? 0 : events.back().timeMs;
    if (report != nullptr)
    {
      pedalSpans(pedal, 0, endMs, report->writtenPedal);
    }
    std::vector<RollEvent> played = spacePedal(pedal);
    if (volume != 100)
    {
      scaleVelocities(notes, volume);
    }
    if (timing != nullptr && timing->hasLanding())
    {
      markPedal(notes, played, *timing);
      allowForLanding(notes, played, *timing);
      roll.leadMs = timing->maxLandingMs();
    }
    separateRepeats(notes, roll);
    roll.events.insert(roll.events.end(), played.begin(), played.end());
    if (report != nullptr)
    {
      reportNotes(notes, doubled, roll.leadMs, *report);
      pedalSpans(roll.events, roll.leadMs, endMs, report->playedPedal); // only the pedal is in there yet
    }

    for (const Note &note : notes)
    {
      if (note.velocity == 0)
      {
        continue;
      }
      roll.events.push_back({note.onMs, RollEventType::KeyPress, note.key, note.velocity, note.pedal});
      roll.events.push_back({note.offMs, RollEventType::KeyRelease, note.key, 0, false});
      roll.noteCount++;
    }

    std::stable_sort(roll.events.begin(), roll.events.end(), [](const RollEvent &a, const RollEvent &b)
                     {
      if (a.timeMs != b.timeMs)
      {
        return a.timeMs < b.timeMs;
      }
      return eventOrder(a.type) < eventOrder(b.type); });

    roll.events.shrink_to_fit();
    roll.durationMs = roll.events.empty() ? 0 : roll.events.back().timeMs;
    return true;
  }
  catch (const std::bad_alloc &)
  {
    roll.events.clear();
    if (report != nullptr)
    {
      *report = RollReport();
    }
    return false;
  }
}

RollStart findRollStart(const PianoRoll &roll, uint32_t fromMs, const VelocityTable *timing)
{
  RollStart start;
  start.timeMs = fromMs > roll.leadMs ? fromMs - roll.leadMs : 0;
  start.pedalDown = false;
  start.index = std::lower_bound(roll.events.begin(), roll.events.end(), start.timeMs,
                                 [](const RollEvent &event, uint32_t timeMs)
                                 { return event.timeMs < timeMs; }) -
                roll.events.begin();
  for (size_t i = 0; i < start.index; i++)
  {
    const RollEvent &event = roll.events[i];
    switch (event.type)
    {
    case RollEventType::KeyPress:
      start.skipReleases.set(event.key);
      break;
    case RollEventType::KeyRelease:
      start.skipReleases.reset(event.key);
      break;
    case RollEventType::SustainOn:
      start.pedalDown = true;
      break;
    case RollEventType::SustainOff:
      start.pedalDown = false;
      break;
    }
  }
  if (timing != nullptr && timing->hasLanding())
  {
    for (size_t i = start.index; i < roll.events.size() && roll.events[i].timeMs < fromMs; i++)
    {
      const RollEvent &event = roll.events[i];
      if (event.type == RollEventType::KeyPress &&
          event.timeMs + timing->landingMs(event.key, event.velocity, event.pedal) < fromMs)
      {
        start.skippedPresses.push_back(i);
        start.skipReleases.set(event.key);
      }
    }
  }
  return start;
}
