// Repeats: notes on a key soon after its last strike, which the key may not
// be able to strike again in time (the limits measured on this piano, in the
// PlayConfig). Part of the roll builder (roll_builder.cpp).
#include "roll_notes.h"
#include "app/piano/roll_builder.h"
#include "app/piano/velocity_table.h"
#include <algorithm>
#include <new>

namespace
{
  using namespace roll_notes;

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
} // namespace

namespace roll_notes
{
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
} // namespace roll_notes
