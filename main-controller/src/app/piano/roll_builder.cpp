#include "app/piano/roll_builder.h"
#include "app/piano/velocity_table.h"
#include "app/piano/play_config.h"
#include "roll_notes.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>

using namespace roll_notes;

namespace
{
  const uint8_t LOWEST_PIANO_NOTE = 21; // A0
  bool isPianoNote(uint8_t note)
  {
    return note >= LOWEST_PIANO_NOTE && note < LOWEST_PIANO_NOTE + PIANO_KEY_COUNT;
  }

  // A time in the MIDI, played at speed (percent of the written tempo).
  uint32_t atSpeed(uint32_t ms, uint16_t speed)
  {
    if (speed == 100 || speed == 0)
    {
      return ms;
    }
    return (uint32_t)(((uint64_t)ms * 100 + speed / 2) / speed);
  }

  // Pairs note-ons with note-offs and picks out sustain changes. A note-on
  // for a key that's still down ends the sounding note there; the new note
  // then lasts until every overlapping note on that key has ended. A note-on
  // at the same moment as the sounding note (two voices sharing it, or a
  // duplicate in the file) is one strike, at the louder velocity; it goes in
  // doubled, for the report.
  void collectNotesAndPedal(const std::vector<MidiEvent> &events, uint16_t speed, uint16_t mutedParts,
                            std::vector<Note> &notes, std::vector<RollEvent> &pedal, std::vector<Doubled> &doubled,
                            std::vector<Note> &muted, PianoRoll &roll)
  {
    const size_t NO_NOTE = SIZE_MAX;
    size_t soundingNote[PIANO_KEY_COUNT];
    std::fill(soundingNote, soundingNote + PIANO_KEY_COUNT, NO_NOTE);
    uint8_t heldCount[PIANO_KEY_COUNT] = {};
    bool sustain = false;

    for (const MidiEvent &event : events)
    {
      uint32_t timeMs = atSpeed(event.timeMs, speed);
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
        if (event.part < 16 && (mutedParts >> event.part) & 1)
        {
          // Left out, for the report only: until its note-off (below).
          muted.push_back({timeMs, UINT32_MAX, key, 0, 0, timeMs, UINT32_MAX, event.value, NOTE_MUTED, 0, false,
                           event.part, false});
          break;
        }
        if (soundingNote[key] != NO_NOTE && notes[soundingNote[key]].writtenOnMs == timeMs)
        {
          Note &first = notes[soundingNote[key]];
          Note copy = {timeMs, UINT32_MAX, key, 0, 0, timeMs, 0, event.value, 0, timeMs, false,
                       event.part, false};
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
          notes[soundingNote[key]].offMs = timeMs;
        }
        notes.push_back({timeMs, UINT32_MAX, key, event.value, 0, timeMs, 0, event.value, 0, 0, false,
                         event.part, false});
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
        if (event.part < 16 && (mutedParts >> event.part) & 1)
        {
          for (Note &note : muted)
          {
            if (note.key == key && note.part == event.part && note.writtenOffMs == UINT32_MAX)
            {
              note.writtenOffMs = timeMs;
              break;
            }
          }
          break;
        }
        if (heldCount[key] == 0)
        {
          break; // note-off without a matching note-on
        }
        if (--heldCount[key] == 0)
        {
          notes[soundingNote[key]].offMs = timeMs;
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
          pedal.push_back({timeMs, on ? RollEventType::SustainOn : RollEventType::SustainOff, 0, 0, false, false, 0, 0});
        }
        break;
      }
      }
    }

    // Notes never turned off end with the file.
    uint32_t endMs = events.empty() ? 0 : atSpeed(events.back().timeMs, speed);
    for (Note &note : muted)
    {
      note.writtenOffMs = std::min(note.writtenOffMs, endMs);
      note.offMs = note.writtenOffMs;
    }
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
  // the key's whole travel. The pedal, as played, went down pedalSettleMs
  // before the earliest the key could be pressed (its pedal-up press), and
  // doesn't start lifting before the note sounds. In any doubt a note is
  // pedal-up: a pedal-down duty played with the dampers still on the strings,
  // or falling back, may not sound at all, while a pedal-up one at worst plays
  // a little loud. Times as written, before the lead.
  void markPedal(std::vector<Note> &notes, const std::vector<RollEvent> &pedal, const VelocityTable &timing,
                 const PlayConfig &config)
  {
    for (Note &note : notes)
    {
      int64_t settledBy = (int64_t)note.onMs - timing.landingMs(note.key, note.velocity) - config.pedalSettleMs;
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

  // Shapes the velocities, in this order (each on the result of the last):
  //  1. each part's offset is added, to bring out a hand or a voice;
  //  2. voicing: the notes of each chord (starting within CHORD_WINDOW_MS of
  //     its first) are stretched away from the chord's average by voicing
  //     percent, so whatever voicing the file has stands out more (or less);
  //  3. contrast: every note is stretched away from the piece's median by
  //     contrast percent, so soft passages get softer and loud ones louder;
  //  4. volume: every velocity times volume percent.
  // Rounded once at the end, and kept within 1-127. With volume alone it's
  // exactly velocity * volume / 100, rounded. Before landing times and
  // repeats are worked out, which depend on velocity. The play page
  // (data/www/play/app.js shapeVelocities) does the same, to preview it.
  void shapeVelocities(std::vector<Note> &notes, const Dynamics &dynamics)
  {
    if (dynamics == Dynamics())
    {
      return;
    }
    std::vector<double> shaped(notes.size());
    for (size_t i = 0; i < notes.size(); i++)
    {
      shaped[i] = notes[i].velocity + dynamics.partOffset[std::min<uint8_t>(notes[i].part, MAX_MIDI_PARTS - 1)];
    }
    if (dynamics.voicing != 100)
    {
      // Notes are in the order they're written.
      for (size_t first = 0; first < notes.size();)
      {
        size_t end = first + 1;
        while (end < notes.size() && notes[end].writtenOnMs - notes[first].writtenOnMs < CHORD_WINDOW_MS)
        {
          end++;
        }
        if (end - first > 1)
        {
          double average = 0;
          for (size_t i = first; i < end; i++)
          {
            average += shaped[i];
          }
          average /= end - first;
          for (size_t i = first; i < end; i++)
          {
            shaped[i] = average + (shaped[i] - average) * dynamics.voicing / 100.0;
          }
        }
        first = end;
      }
    }
    if (dynamics.contrast != 100 && !shaped.empty())
    {
      std::vector<double> sorted = shaped;
      std::sort(sorted.begin(), sorted.end());
      size_t middle = sorted.size() / 2;
      double median = sorted.size() % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
      for (double &velocity : shaped)
      {
        velocity = median + (velocity - median) * dynamics.contrast / 100.0;
      }
    }
    for (size_t i = 0; i < notes.size(); i++)
    {
      double rounded = std::floor((shaped[i] * dynamics.volume + 50) / 100);
      notes[i].velocity = (uint8_t)std::min(std::max(rounded, 1.0), 127.0);
      if (notes[i].velocity != rounded)
      {
        notes[i].changes |= NOTE_VELOCITY_LIMITED;
      }
    }
  }

  // The pedal solenoids need pedalMinMs between changes to finish moving.
  // Changes are checked against the last one kept, so delays never pile up:
  // - release then a quick re-press (a pedal change): the release stays on
  //   time, so the old harmony is cleared, and the re-press waits.
  // - press then a quick release: too brief to matter, so both are dropped.
  // pedal alternates on/off, starting with on.
  std::vector<RollEvent> spacePedal(const std::vector<RollEvent> &pedal, const PlayConfig &config)
  {
    std::vector<RollEvent> kept;
    for (RollEvent change : pedal)
    {
      if (!kept.empty() && change.timeMs < kept.back().timeMs + config.pedalMinMs)
      {
        if (kept.back().type == RollEventType::SustainOn)
        {
          kept.pop_back();
          continue;
        }
        change.timeMs = kept.back().timeMs + config.pedalMinMs;
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
  void reportNotes(const std::vector<Note> &notes, const std::vector<Doubled> &doubled,
                   const std::vector<Note> &muted, uint32_t lead, RollReport &report)
  {
    report.notes.reserve(notes.size() + doubled.size() + muted.size());
    for (const Note &note : muted)
    {
      report.notes.push_back({note.writtenOnMs, note.writtenOffMs, 0, 0, 0, note.key, note.writtenVelocity, 0,
                              note.part, note.changes});
    }
    for (const Note &note : notes)
    {
      NoteReport out = {note.writtenOnMs, note.writtenOffMs, 0, 0, note.mergedIntoMs, note.key, note.writtenVelocity,
                        note.velocity, note.part, note.changes};
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
                              note.writtenVelocity, 0, note.part, note.changes});
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
  Dynamics dynamics;
  dynamics.volume = volume;
  return buildPianoRoll(events, roll, timing, dynamics, report);
}

bool buildPianoRoll(const std::vector<MidiEvent> &events, PianoRoll &roll, const VelocityTable *timing,
                    const Dynamics &dynamics, RollReport *report, const PlayConfig *playConfig)
{
  static const PlayConfig DEFAULTS;
  const PlayConfig &config = playConfig != nullptr ? *playConfig : DEFAULTS;
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
  roll.dynamics = dynamics;
  roll.leadMs = 0;

  try
  {
    std::vector<Note> notes;
    std::vector<RollEvent> pedal;
    std::vector<Doubled> doubled;
    std::vector<Note> muted;
    collectNotesAndPedal(events, dynamics.speed, dynamics.mutedParts, notes, pedal, doubled, muted, roll);
    uint32_t endMs = events.empty() ? 0 : atSpeed(events.back().timeMs, dynamics.speed);
    if (report != nullptr)
    {
      pedalSpans(pedal, 0, endMs, report->writtenPedal);
    }
    std::vector<RollEvent> played = spacePedal(pedal, config);
    shapeVelocities(notes, dynamics);
    if (timing != nullptr && timing->hasLanding())
    {
      markPedal(notes, played, *timing, config);
      allowForLanding(notes, played, *timing);
      roll.leadMs = timing->maxLandingMs();
    }
    separateRepeats(notes, roll, timing, dynamics.repeatBoost, config);
    roll.events.insert(roll.events.end(), played.begin(), played.end());
    if (report != nullptr)
    {
      reportNotes(notes, doubled, muted, roll.leadMs, *report);
      pedalSpans(roll.events, roll.leadMs, endMs, report->playedPedal); // only the pedal is in there yet
    }

    for (const Note &note : notes)
    {
      if (note.velocity == 0)
      {
        continue;
      }
      // The strike itself: note.landingMs is when it sounds (a repeat's press
      // can be moved), not when its hammer lands.
      uint16_t duty = timing != nullptr ? timing->duty(note.key, note.velocity, note.pedal)
                                        : VelocityTable::defaultDuty(note.velocity);
      uint16_t landing = timing != nullptr ? timing->landingMs(note.key, note.velocity, note.pedal) : 0;
      roll.events.push_back(
          {note.onMs, RollEventType::KeyPress, note.key, note.velocity, note.pedal, note.repeat, duty, landing});
      roll.events.push_back({note.offMs, RollEventType::KeyRelease, note.key, 0, false, false, 0, 0});
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
          event.timeMs + event.landingMs < fromMs)
      {
        start.skippedPresses.push_back(i);
        start.skipReleases.set(event.key);
      }
    }
  }
  return start;
}
