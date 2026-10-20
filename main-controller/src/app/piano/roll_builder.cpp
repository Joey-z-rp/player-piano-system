#include "app/piano/roll_builder.h"
#include "app/piano/velocity_table.h"
#include "app/piano/play_config.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>

namespace
{
  const uint8_t LOWEST_PIANO_NOTE = 21; // A0
  // The timing rules (how long notes are held, how soon a key can strike
  // again, the pedal) come from a PlayConfig: see play_config.h.

  // Whether a key can strike again intervalMs after the note before
  // (velocity first) sounded, at velocity second.
  bool canRepeat(const PlayConfig &config, uint8_t first, uint8_t second, int32_t intervalMs)
  {
    for (uint8_t i = 0; i < config.repeatLimitCount; i++)
    {
      const RepeatLimit &limit = config.repeatLimits[i];
      if (second < limit.from)
      {
        continue;
      }
      bool afterLoud = first >= config.repeatLoudFrom;
      if (intervalMs < (afterLoud ? limit.afterLoudMs : limit.afterSoftMs))
      {
        return false;
      }
      return afterLoud || second < config.deadZoneFromVelocity || second > config.deadZoneToVelocity ||
             intervalMs < config.deadZoneFromMs || intervalMs >= config.deadZoneToMs;
    }
    return false;
  }

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
    uint8_t part;          // see MidiEvent::part
    bool repeat;           // struck within PlayConfig::repeatWindowMs of the last strike on its key
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
          Note copy = {event.timeMs, UINT32_MAX, key, 0, 0, event.timeMs, 0, event.value, 0, event.timeMs, false,
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
          notes[soundingNote[key]].offMs = event.timeMs;
        }
        notes.push_back({event.timeMs, UINT32_MAX, key, event.value, 0, event.timeMs, 0, event.value, 0, 0, false,
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
          pedal.push_back({event.timeMs, on ? RollEventType::SustainOn : RollEventType::SustainOff, 0, 0, false, false});
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

  // When a note's hammer hits the string, on the roll's timeline.
  uint32_t soundsMs(const Note &note)
  {
    return note.onMs + note.landingMs;
  }

  // Holds note in the strike kept until it ends. If note is louder, kept is
  // struck at its velocity instead, and so re-timed for that hammer's landing
  // to sound when the earlier of the two should. A louder hammer is faster, so
  // that's no earlier than kept was pressed; with a table where it isn't, it's
  // kept late enough for the strike before to be released first.
  void mergeRepeat(Note &kept, Note &note, Note *before, const PlayConfig &config)
  {
    if (note.velocity > kept.velocity)
    {
      uint32_t sounds = std::min(soundsMs(kept), soundsMs(note));
      uint32_t onMs = sounds - note.landingMs;
      if (before != nullptr)
      {
        onMs = std::max(onMs, soundsMs(*before) + config.minNoteMs + 1);
        before->offMs =
            std::max(soundsMs(*before) + config.minNoteMs, std::min(before->offMs, onMs - config.restrikeGapMs));
      }
      kept.onMs = onMs;
      kept.velocity = note.velocity;
      kept.landingMs = note.landingMs;
      kept.offMs = std::max(kept.offMs, soundsMs(kept) + config.minNoteMs);
    }
    kept.offMs = std::max(kept.offMs, note.offMs);
    kept.changes |= NOTE_ABSORBED;
    note.changes |= NOTE_MERGED;
    note.mergedIntoMs = kept.writtenOnMs;
    note.velocity = 0;
  }

  // A key can strike again once canRepeat allows it (how soon was measured on
  // this piano, and depends mostly on how loud the repeat is). It's released
  // restrikeGapMs before the next press where there's time, but never
  // sooner than minNoteMs after its hammer lands: for a close repeat, that's
  // as soon as it can be, giving the key the most time to come back up. A
  // repeat that comes sooner than allowed can't be a new strike, so it's
  // merged into the one before it: the key stays down until the later note
  // ends, at the louder velocity, sounding when the first of them should. A
  // fast run of repeats keeps its first onset and plays as many strikes as the
  // key can manage; a grace note followed by a long note becomes one long note.
  // Notes come in press order, so with landing times a soft note can come
  // before a louder one written a little earlier (or at the same time, as when
  // two voices share a note).
  //
  // With a boost, a repeat is played that many velocities louder (up to 127),
  // when that lets it strike again where it otherwise couldn't, or anyway when
  // it can: louder repeats can follow sooner, and a repeat starts from a key
  // still moving, so it can come out softer. Its press is timed for the louder
  // velocity's landing. A repeat that's merged isn't boosted.
  void separateRepeats(std::vector<Note> &notes, PianoRoll &roll, const VelocityTable *timing, uint8_t boost,
                       const PlayConfig &config)
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
      note.offMs = std::max(note.offMs, soundsMs(note) + config.minNoteMs);
      if (kept != nullptr && kept->key == note.key)
      {
        int32_t interval = (int32_t)soundsMs(note) - (int32_t)soundsMs(*kept);
        bool repeat = interval < (int32_t)config.repeatWindowMs;
        uint32_t earliestRelease = soundsMs(*kept) + config.minNoteMs;
        if (repeat && boost > 0 && note.velocity < 127)
        {
          // When the hammer lands at the louder velocity: the note sounds at
          // the same moment, so it's pressed later.
          uint8_t louder = (uint8_t)std::min<uint32_t>(note.velocity + boost, 127);
          uint32_t landing = timing != nullptr && timing->hasLanding()
                                 ? timing->landingMs(note.key, louder, note.pedal)
                                 : note.landingMs;
          uint32_t sounds = soundsMs(note);
          uint32_t onMs = sounds - std::min(sounds, landing);
          uint32_t pressMs = onMs - std::min<uint32_t>(onMs, config.repeatLateMs);
          if (canRepeat(config, kept->velocity, louder, interval) && pressMs > earliestRelease)
          {
            note.velocity = louder;
            note.landingMs = landing;
            note.onMs = onMs;
            note.changes |= NOTE_REPEAT_BOOSTED;
          }
        }
        uint32_t pressMs = repeat ? note.onMs - std::min<uint32_t>(note.onMs, config.repeatLateMs) : note.onMs;
        if (!canRepeat(config, kept->velocity, note.velocity, interval) || pressMs <= earliestRelease)
        {
          mergeRepeat(*kept, note, before, config);
          roll.mergedNotes++;
          continue;
        }
        if (repeat)
        {
          note.repeat = true;
          note.landingMs += note.onMs - pressMs;
          note.onMs = pressMs;
          note.offMs = std::max(note.offMs, soundsMs(note) + config.minNoteMs);
        }
        kept->offMs = std::max(earliestRelease, std::min(kept->offMs, note.onMs - config.restrikeGapMs));
        before = kept;
      }
      else
      {
        before = nullptr;
      }
      kept = &note;
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
  void reportNotes(const std::vector<Note> &notes, const std::vector<Doubled> &doubled, uint32_t lead,
                   RollReport &report)
  {
    report.notes.reserve(notes.size() + doubled.size());
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
    collectNotesAndPedal(events, notes, pedal, doubled, roll);
    uint32_t endMs = events.empty() ? 0 : events.back().timeMs;
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
      reportNotes(notes, doubled, roll.leadMs, *report);
      pedalSpans(roll.events, roll.leadMs, endMs, report->playedPedal); // only the pedal is in there yet
    }

    for (const Note &note : notes)
    {
      if (note.velocity == 0)
      {
        continue;
      }
      roll.events.push_back({note.onMs, RollEventType::KeyPress, note.key, note.velocity, note.pedal, note.repeat});
      roll.events.push_back({note.offMs, RollEventType::KeyRelease, note.key, 0, false, false});
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
