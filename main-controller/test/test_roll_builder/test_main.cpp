#include <unity.h>
#include <algorithm>
#include "app/piano/roll_builder.h"
#include "app/piano/velocity_table.h"

// MIDI note 60 is middle C, piano key 39.
static const uint8_t C4 = 60;
static const uint8_t C4_KEY = 39;
static const uint8_t D4 = 62;
static const uint8_t D4_KEY = 41;

static PianoRoll roll;

void setUp()
{
  roll = PianoRoll();
}

void tearDown()
{
}

static MidiEvent on(uint32_t timeMs, uint8_t note, uint8_t velocity = 64)
{
  return {timeMs, MidiEventType::NoteOn, note, velocity};
}

static MidiEvent off(uint32_t timeMs, uint8_t note)
{
  return {timeMs, MidiEventType::NoteOff, note, 0};
}

static MidiEvent pedal(uint32_t timeMs, uint8_t value)
{
  return {timeMs, MidiEventType::Sustain, 0, value};
}

static void build(std::vector<MidiEvent> events, const VelocityTable *timing = nullptr)
{
  TEST_ASSERT_TRUE(buildPianoRoll(events, roll, timing));
}

// A table whose hammers land 150 - velocity ms after the press: 149 ms at
// velocity 1 (the latest, so the lead), 130 at 20, 50 at 100.
static VelocityTable landingTable(bool withLanding = true)
{
  std::string json = "{\"keys\":{}";
  if (withLanding)
  {
    json += ",\"landing_ms\":[";
    for (int velocity = 1; velocity <= 127; velocity++)
    {
      json += (velocity > 1 ? "," : "") + std::to_string(150 - velocity);
    }
    json += "]";
  }
  json += "}";
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(table.load(json.data(), json.size(), error), error.c_str());
  return table;
}

struct Expected
{
  uint32_t timeMs;
  RollEventType type;
  uint8_t key;
  uint8_t velocity;
};

static void assertRoll(std::vector<Expected> expected)
{
  TEST_ASSERT_EQUAL_MESSAGE(expected.size(), roll.events.size(), "event count");
  for (size_t i = 0; i < expected.size(); i++)
  {
    char message[48];
    snprintf(message, sizeof(message), "event %u", (unsigned)i);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected[i].timeMs, roll.events[i].timeMs, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)expected[i].type, (int)roll.events[i].type, message);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected[i].key, roll.events[i].key, message);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected[i].velocity, roll.events[i].velocity, message);
  }
}

static const RollEventType PRESS = RollEventType::KeyPress;
static const RollEventType RELEASE = RollEventType::KeyRelease;
static const RollEventType PEDAL_DOWN = RollEventType::SustainOn;
static const RollEventType PEDAL_UP = RollEventType::SustainOff;

// --- keys ---

void test_notes_map_to_keys()
{
  build({on(0, 21, 10), on(0, 108, 20), off(500, 21), off(500, 108)});
  assertRoll({{0, PRESS, 0, 10}, {0, PRESS, 87, 20}, {500, RELEASE, 0, 0}, {500, RELEASE, 87, 0}});
  TEST_ASSERT_EQUAL_UINT32(2, roll.noteCount);
  TEST_ASSERT_EQUAL_UINT32(500, roll.durationMs);
}

void test_notes_outside_piano_are_skipped()
{
  build({on(0, 20), off(100, 20), on(0, 109), off(100, 109), on(0, C4), off(100, C4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {100, RELEASE, C4_KEY, 0}});
  TEST_ASSERT_EQUAL_UINT32(2, roll.skippedNotes);
}

void test_unmatched_note_off_is_ignored()
{
  build({off(0, C4), on(100, C4), off(200, C4)});
  assertRoll({{100, PRESS, C4_KEY, 64}, {200, RELEASE, C4_KEY, 0}});
}

void test_note_never_released_ends_with_last_event()
{
  build({on(0, C4), on(100, D4), off(300, D4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {100, PRESS, D4_KEY, 64}, {300, RELEASE, C4_KEY, 0}, {300, RELEASE, D4_KEY, 0}});
}

void test_short_note_is_lengthened()
{
  build({on(0, C4), off(5, C4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {10, RELEASE, C4_KEY, 0}});
}

// --- repeated notes on one key ---

void test_overlapping_note_waits_for_its_own_note_off()
{
  // Two hands on one key: B's strike ends A, and A's note-off must not end B.
  build({on(0, C4, 50), on(500, C4, 60), off(700, C4), off(1000, C4)});
  assertRoll({{0, PRESS, C4_KEY, 50}, {430, RELEASE, C4_KEY, 0}, {500, PRESS, C4_KEY, 60}, {1000, RELEASE, C4_KEY, 0}});
}

void test_repeat_with_short_gap_is_released_earlier()
{
  build({on(0, C4), off(500, C4), on(520, C4), off(800, C4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {450, RELEASE, C4_KEY, 0}, {520, PRESS, C4_KEY, 64}, {800, RELEASE, C4_KEY, 0}});
}

void test_repeat_with_no_gap_is_released_earlier()
{
  build({on(0, C4), off(300, C4), on(300, C4), off(600, C4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {230, RELEASE, C4_KEY, 0}, {300, PRESS, C4_KEY, 64}, {600, RELEASE, C4_KEY, 0}});
}

void test_repeat_is_sent_early_and_marked()
{
  // 200 ms after the last C4 is a repeat: sent 10 ms early (its hammer lands
  // later) and marked, so it's clamped as one.
  build({on(0, C4), off(100, C4), on(200, C4), off(300, C4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {100, RELEASE, C4_KEY, 0}, {190, PRESS, C4_KEY, 64}, {300, RELEASE, C4_KEY, 0}});
  TEST_ASSERT_EQUAL_UINT32(0, roll.mergedNotes);
  TEST_ASSERT_FALSE(roll.events[0].repeat);
  TEST_ASSERT_TRUE(roll.events[2].repeat);
}

void test_strike_long_after_the_last_is_not_a_repeat()
{
  build({on(0, C4), off(100, C4), on(300, C4), off(400, C4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {100, RELEASE, C4_KEY, 0}, {300, PRESS, C4_KEY, 64}, {400, RELEASE, C4_KEY, 0}});
  TEST_ASSERT_FALSE(roll.events[2].repeat);
}

void test_repeat_limits_follow_the_repeats_velocity()
{
  // After velocity 64: a loud repeat (112) plays from 90 ms, a medium one (64) from 150.
  build({on(0, C4, 64), off(50, C4), on(100, C4, 112), off(200, C4)});
  TEST_ASSERT_EQUAL_UINT32(0, roll.mergedNotes);
  build({on(0, C4, 64), off(50, C4), on(140, C4, 64), off(250, C4)});
  TEST_ASSERT_EQUAL_UINT32(1, roll.mergedNotes);
  build({on(0, C4, 64), off(50, C4), on(150, C4, 64), off(250, C4)});
  TEST_ASSERT_EQUAL_UINT32(0, roll.mergedNotes);
  // After a loud note (112), a loud repeat needs 120 ms.
  build({on(0, C4, 112), off(50, C4), on(110, C4, 112), off(200, C4)});
  TEST_ASSERT_EQUAL_UINT32(1, roll.mergedNotes);
  build({on(0, C4, 112), off(50, C4), on(120, C4, 112), off(200, C4)});
  TEST_ASSERT_EQUAL_UINT32(0, roll.mergedNotes);
}

void test_repeats_around_88_skip_their_dead_zone()
{
  // After a softer note, a repeat at 88 plays at 90-104 ms and from 120, not in between.
  for (uint32_t interval : {95u, 110u, 125u})
  {
    build({on(0, C4, 64), off(50, C4), on(interval, C4, 88), off(300, C4)});
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(interval == 110 ? 1 : 0, roll.mergedNotes, "interval");
  }
}

void test_fast_repeats_keep_as_many_strikes_as_possible()
{
  // 4 strikes 60 ms apart at velocity 64, which can repeat from 150 ms: the
  // first and fourth play, the fourth sent 10 ms early as a repeat.
  build({on(0, C4), off(50, C4), on(60, C4), off(110, C4), on(120, C4), off(170, C4), on(180, C4), off(230, C4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {100, RELEASE, C4_KEY, 0}, {170, PRESS, C4_KEY, 64}, {230, RELEASE, C4_KEY, 0}});
  TEST_ASSERT_EQUAL_UINT32(2, roll.noteCount);
  TEST_ASSERT_EQUAL_UINT32(2, roll.mergedNotes);
}

void test_grace_note_merges_into_main_note_at_louder_velocity()
{
  build({on(1000, C4, 30), off(1030, C4), on(1050, C4, 110), off(2000, C4)});
  assertRoll({{1000, PRESS, C4_KEY, 110}, {2000, RELEASE, C4_KEY, 0}});
  TEST_ASSERT_EQUAL_UINT32(1, roll.mergedNotes);
}

void test_repeats_on_other_keys_are_independent()
{
  build({on(0, C4), on(10, D4), off(20, C4), off(30, D4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {10, PRESS, D4_KEY, 64}, {20, RELEASE, C4_KEY, 0}, {30, RELEASE, D4_KEY, 0}});
}

// --- sustain pedal ---

void test_pedal_threshold_and_repeats()
{
  // 64 and up is down; repeated values don't create changes.
  build({pedal(0, 64), pedal(50, 127), pedal(500, 63), pedal(600, 0)});
  assertRoll({{0, PEDAL_DOWN, 0, 0}, {500, PEDAL_UP, 0, 0}});
}

void test_quick_repedal_delays_the_press()
{
  build({pedal(0, 127), pedal(500, 0), pedal(520, 127), pedal(1000, 0)});
  assertRoll({{0, PEDAL_DOWN, 0, 0}, {500, PEDAL_UP, 0, 0}, {600, PEDAL_DOWN, 0, 0}, {1000, PEDAL_UP, 0, 0}});
}

void test_brief_pedal_press_is_dropped()
{
  build({pedal(1000, 127), pedal(1030, 0)});
  assertRoll({});
}

void test_pedal_delays_do_not_pile_up()
{
  // The re-press at 2510 moves to 2600, then the release at 2650 is too soon
  // after it, so both go; the press at 2700 is on time.
  build({pedal(2000, 127), pedal(2500, 0), pedal(2510, 127), pedal(2650, 0), pedal(2700, 127), pedal(3000, 0)});
  assertRoll({{2000, PEDAL_DOWN, 0, 0}, {2500, PEDAL_UP, 0, 0}, {2700, PEDAL_DOWN, 0, 0}, {3000, PEDAL_UP, 0, 0}});
}

// --- allowing for when the hammer lands ---

void test_presses_are_sent_early_by_their_landing_time()
{
  VelocityTable table = landingTable();
  build({on(1000, C4, 100), on(1000, D4, 20), off(1500, C4), off(1500, D4)}, &table);
  // Both sound at 1000 + 149: D4's soft hammer takes 130 ms, C4's 50 ms.
  assertRoll({{1019, PRESS, D4_KEY, 20}, {1099, PRESS, C4_KEY, 100}, {1649, RELEASE, C4_KEY, 0}, {1649, RELEASE, D4_KEY, 0}});
  TEST_ASSERT_EQUAL_UINT32(1649, roll.durationMs);
}

void test_short_note_is_held_until_after_it_lands()
{
  VelocityTable table = landingTable();
  build({on(0, C4, 20), off(10, C4)}, &table);
  assertRoll({{19, PRESS, C4_KEY, 20}, {19 + 130 + 10, RELEASE, C4_KEY, 0}});
}

void test_pedal_moves_by_the_lead()
{
  VelocityTable table = landingTable();
  build({pedal(0, 127), pedal(500, 0)}, &table);
  assertRoll({{149, PEDAL_DOWN, 0, 0}, {649, PEDAL_UP, 0, 0}});
}

void test_soft_repeats_need_time_for_the_hammer_to_land()
{
  VelocityTable table = landingTable();
  // A soft repeat (velocity 20) needs 195 ms...
  build({on(0, C4, 20), off(100, C4), on(150, C4, 20), off(300, C4)}, &table);
  TEST_ASSERT_EQUAL_UINT32(1, roll.mergedNotes);
  // ...and the first note is held until 70 ms before the second press.
  build({on(0, C4, 20), off(100, C4), on(300, C4, 20), off(400, C4)}, &table);
  assertRoll({{19, PRESS, C4_KEY, 20}, {249, RELEASE, C4_KEY, 0}, {319, PRESS, C4_KEY, 20}, {549, RELEASE, C4_KEY, 0}});
}

void test_table_without_landing_times_changes_nothing()
{
  VelocityTable table = landingTable(false);
  build({on(0, C4), off(5, C4), pedal(0, 127), pedal(500, 0)}, &table);
  assertRoll({{0, PEDAL_DOWN, 0, 0}, {0, PRESS, C4_KEY, 64}, {10, RELEASE, C4_KEY, 0}, {500, PEDAL_UP, 0, 0}});
}

// --- volume ---

void test_volume_scales_velocities()
{
  TEST_ASSERT_TRUE(buildPianoRoll({on(0, C4, 100), on(0, D4, 40), off(500, C4), off(500, D4)}, roll, nullptr, 80));
  assertRoll({{0, PRESS, C4_KEY, 80}, {0, PRESS, D4_KEY, 32}, {500, RELEASE, C4_KEY, 0}, {500, RELEASE, D4_KEY, 0}});
  TEST_ASSERT_EQUAL_UINT16(80, roll.dynamics.volume);
}

void test_volume_stays_within_1_and_127()
{
  TEST_ASSERT_TRUE(buildPianoRoll({on(0, C4, 120), on(0, D4, 3), off(500, C4), off(500, D4)}, roll, nullptr, 150));
  TEST_ASSERT_EQUAL_UINT8(127, roll.events[0].velocity);
  TEST_ASSERT_TRUE(buildPianoRoll({on(0, C4, 3), off(500, C4)}, roll, nullptr, 10));
  TEST_ASSERT_EQUAL_UINT8(1, roll.events[0].velocity);
}

void test_volume_times_notes_by_their_new_velocity()
{
  // Landing is 150 - velocity ms: at 50% velocity 100 plays as 50, landing 100 ms after the press.
  VelocityTable table = landingTable();
  TEST_ASSERT_TRUE(buildPianoRoll({on(1000, C4, 100), off(1500, C4)}, roll, &table, 50));
  assertRoll({{1000 + 149 - 100, PRESS, C4_KEY, 50}, {1649, RELEASE, C4_KEY, 0}});
}

// --- assembly ---

void test_same_time_order_is_release_pedal_press()
{
  build({on(0, C4), off(500, C4), pedal(500, 127), on(500, D4), off(900, D4), pedal(900, 0)});
  assertRoll({{0, PRESS, C4_KEY, 64},
              {500, RELEASE, C4_KEY, 0},
              {500, PEDAL_DOWN, 0, 0},
              {500, PRESS, D4_KEY, 64},
              {900, RELEASE, D4_KEY, 0},
              {900, PEDAL_UP, 0, 0}});
  TEST_ASSERT_EQUAL_UINT32(900, roll.durationMs);
}

void test_empty_input()
{
  build({});
  assertRoll({});
  TEST_ASSERT_EQUAL_UINT32(0, roll.durationMs);
  TEST_ASSERT_EQUAL_UINT32(0, roll.noteCount);
}

void test_builder_resets_previous_contents()
{
  build({on(0, C4), off(100, C4), on(0, 10)});
  build({on(0, D4), off(100, D4)});
  assertRoll({{0, PRESS, D4_KEY, 64}, {100, RELEASE, D4_KEY, 0}});
  TEST_ASSERT_EQUAL_UINT32(1, roll.noteCount);
  TEST_ASSERT_EQUAL_UINT32(0, roll.skippedNotes);
}

void test_start_from_the_beginning()
{
  build({pedal(0, 127), on(0, C4), off(500, C4)});
  RollStart start = findRollStart(roll, 0);
  TEST_ASSERT_EQUAL(0, start.index);
  TEST_ASSERT_EQUAL_UINT32(0, start.timeMs);
  TEST_ASSERT_FALSE(start.pedalDown);
  TEST_ASSERT_TRUE(start.skipReleases.none());
}

void test_start_part_way_knows_held_keys_and_the_pedal()
{
  build({pedal(0, 127), on(0, C4), on(1000, D4), off(1200, D4), off(2000, C4), on(3000, D4), off(3200, D4)});
  RollStart start = findRollStart(roll, 1100);
  TEST_ASSERT_EQUAL_UINT32(1100, start.timeMs); // no landing times, so no lead
  TEST_ASSERT_TRUE(start.pedalDown);
  TEST_ASSERT_TRUE(start.skipReleases.test(C4_KEY));
  TEST_ASSERT_TRUE(start.skipReleases.test(D4_KEY));
  TEST_ASSERT_EQUAL(2, start.skipReleases.count());
  const RollEvent &first = roll.events[start.index];
  TEST_ASSERT_EQUAL_UINT32(1200, first.timeMs);
  TEST_ASSERT_EQUAL(RollEventType::KeyRelease, first.type);
  TEST_ASSERT_EQUAL(D4_KEY, first.key);
}

void test_start_after_the_pedal_is_lifted()
{
  build({pedal(0, 127), on(0, C4), off(500, C4), pedal(800, 0), on(1000, D4), off(1500, D4)});
  RollStart start = findRollStart(roll, 900);
  TEST_ASSERT_FALSE(start.pedalDown);
  TEST_ASSERT_TRUE(start.skipReleases.none());
  TEST_ASSERT_EQUAL_UINT32(1000, roll.events[start.index].timeMs);
}

void test_start_moves_back_by_the_lead()
{
  VelocityTable table = landingTable();
  build({on(1000, C4, 100), off(1500, C4)}, &table);
  TEST_ASSERT_EQUAL_UINT32(149, roll.leadMs);
  // The note sounds at 1149 (1000 + the lead); its press goes 50 ms before.
  RollStart start = findRollStart(roll, 1149);
  TEST_ASSERT_EQUAL_UINT32(1000, start.timeMs);
  const RollEvent &first = roll.events[start.index];
  TEST_ASSERT_EQUAL(RollEventType::KeyPress, first.type);
  TEST_ASSERT_EQUAL_UINT32(1099, first.timeMs);
  TEST_ASSERT_TRUE(start.skipReleases.none());
  TEST_ASSERT_TRUE(start.skippedPresses.empty());
  // Within the lead of the beginning, it starts at the beginning.
  TEST_ASSERT_EQUAL_UINT32(0, findRollStart(roll, 100).timeMs);
}

void test_start_skips_presses_that_sound_before_it()
{
  VelocityTable table = landingTable();
  // D4 sounds at 1149 + 10 (pressed at 1109), C4 at 1149 + 40 (pressed at 1139).
  build({on(1010, D4, 100), off(1500, D4), on(1040, C4, 100), off(1500, C4)}, &table);
  RollStart start = findRollStart(roll, 1149 + 20, &table);
  TEST_ASSERT_EQUAL(1, start.skippedPresses.size());
  const RollEvent &skipped = roll.events[start.skippedPresses[0]];
  TEST_ASSERT_EQUAL(RollEventType::KeyPress, skipped.type);
  TEST_ASSERT_EQUAL(D4_KEY, skipped.key);
  TEST_ASSERT_TRUE(start.skipReleases.test(D4_KEY));  // its release isn't played either
  TEST_ASSERT_FALSE(start.skipReleases.test(C4_KEY)); // still to sound, so pressed
  // Without landing times nothing is skipped.
  TEST_ASSERT_TRUE(findRollStart(roll, 1169).skippedPresses.empty());
}

void test_start_at_the_end_plays_nothing()
{
  build({on(0, C4), off(500, C4)});
  RollStart start = findRollStart(roll, roll.durationMs + 1);
  TEST_ASSERT_EQUAL(roll.events.size(), start.index);
}

static RollReport report;

static void buildReport(std::vector<MidiEvent> events, const VelocityTable *timing = nullptr, uint16_t volume = 100)
{
  TEST_ASSERT_TRUE(buildPianoRoll(events, roll, timing, volume, &report));
}

void test_report_unchanged_note()
{
  buildReport({on(0, C4, 70), off(500, C4)});
  TEST_ASSERT_EQUAL(1, report.notes.size());
  const NoteReport &note = report.notes[0];
  TEST_ASSERT_EQUAL(C4_KEY, note.key);
  TEST_ASSERT_EQUAL_UINT32(0, note.onMs);
  TEST_ASSERT_EQUAL_UINT32(500, note.writtenOffMs);
  TEST_ASSERT_EQUAL_UINT32(500, note.playedOffMs);
  TEST_ASSERT_EQUAL(70, note.writtenVelocity);
  TEST_ASSERT_EQUAL(70, note.playedVelocity);
  TEST_ASSERT_EQUAL(0, note.changes);
}

void test_report_merged_repeat()
{
  buildReport({on(0, C4, 60), off(40, C4), on(50, C4, 90), off(300, C4)});
  TEST_ASSERT_EQUAL(2, report.notes.size());
  TEST_ASSERT_EQUAL(NOTE_ABSORBED, report.notes[0].changes);
  TEST_ASSERT_EQUAL(90, report.notes[0].playedVelocity);
  TEST_ASSERT_EQUAL_UINT32(300, report.notes[0].playedOffMs);
  TEST_ASSERT_EQUAL(NOTE_MERGED, report.notes[1].changes);
  TEST_ASSERT_EQUAL(0, report.notes[1].playedVelocity);
  TEST_ASSERT_EQUAL_UINT32(0, report.notes[1].playedOffMs);
  TEST_ASSERT_EQUAL(90, report.notes[1].writtenVelocity);
}

void test_report_shortened_and_lengthened()
{
  buildReport({on(0, C4), off(500, C4), on(550, C4), off(800, C4), on(1000, D4), off(1005, D4)});
  TEST_ASSERT_EQUAL(3, report.notes.size());
  TEST_ASSERT_EQUAL(NOTE_SHORTENED, report.notes[0].changes);
  TEST_ASSERT_EQUAL_UINT32(480, report.notes[0].playedOffMs);
  TEST_ASSERT_EQUAL(0, report.notes[1].changes);
  TEST_ASSERT_EQUAL(NOTE_LENGTHENED, report.notes[2].changes);
  TEST_ASSERT_EQUAL_UINT32(1010, report.notes[2].playedOffMs);
}

void test_report_velocity_limited_by_volume()
{
  buildReport({on(0, C4, 100), off(500, C4), on(0, D4, 50), off(500, D4)}, nullptr, 200);
  TEST_ASSERT_EQUAL(NOTE_VELOCITY_LIMITED, report.notes[0].changes);
  TEST_ASSERT_EQUAL(127, report.notes[0].playedVelocity);
  TEST_ASSERT_EQUAL(0, report.notes[1].changes);
  TEST_ASSERT_EQUAL(100, report.notes[1].playedVelocity);
}

void test_report_is_on_the_midi_timeline()
{
  VelocityTable table = landingTable();
  buildReport({on(1000, C4, 100), off(1500, C4), on(2000, D4, 100), off(2005, D4)}, &table);
  TEST_ASSERT_EQUAL_UINT32(1000, report.notes[0].onMs);
  TEST_ASSERT_EQUAL_UINT32(1500, report.notes[0].playedOffMs);
  TEST_ASSERT_EQUAL(0, report.notes[0].changes);
  // Held until 10 ms after its hammer lands, as it sounds.
  TEST_ASSERT_EQUAL_UINT32(2010, report.notes[1].playedOffMs);
  TEST_ASSERT_EQUAL(NOTE_LENGTHENED, report.notes[1].changes);
}

void test_report_pedal_as_written_and_played()
{
  VelocityTable table = landingTable();
  buildReport({pedal(0, 127), pedal(50, 0), pedal(500, 127), on(600, C4), pedal(1000, 0), off(1200, C4)}, &table);
  TEST_ASSERT_EQUAL(2, report.writtenPedal.size());
  TEST_ASSERT_EQUAL_UINT32(0, report.writtenPedal[0].downMs);
  TEST_ASSERT_EQUAL_UINT32(50, report.writtenPedal[0].upMs);
  TEST_ASSERT_EQUAL_UINT32(500, report.writtenPedal[1].downMs);
  TEST_ASSERT_EQUAL_UINT32(1000, report.writtenPedal[1].upMs);
  // The 50 ms press is too brief for the solenoids and is dropped.
  TEST_ASSERT_EQUAL(1, report.playedPedal.size());
  TEST_ASSERT_EQUAL_UINT32(500, report.playedPedal[0].downMs);
  TEST_ASSERT_EQUAL_UINT32(1000, report.playedPedal[0].upMs);
}

void test_doubled_note_is_struck_once_at_the_louder_velocity()
{
  // Two voices share C4 at 1000: the soft one is pressed first (its hammer is
  // slower), so the loud one merges into it, re-timed to sound at 1149.
  VelocityTable table = landingTable();
  buildReport({on(1000, C4, 30), on(1000, C4, 100), off(1500, C4), off(1500, C4)}, &table);
  assertRoll({{1099, PRESS, C4_KEY, 100}, {1649, RELEASE, C4_KEY, 0}});
  TEST_ASSERT_EQUAL(NOTE_ABSORBED, report.notes[0].changes);
  TEST_ASSERT_EQUAL_UINT32(1000, report.notes[0].playedOnMs);
  TEST_ASSERT_EQUAL(NOTE_MERGED, report.notes[1].changes);
  TEST_ASSERT_EQUAL_UINT32(1000, report.notes[1].mergedIntoMs);
}

void test_louder_note_written_just_before_sounds_on_time()
{
  // The loud note at 990 should sound at 1139; the soft one at 1000 is pressed
  // first, so the strike takes the loud velocity and is re-timed for it.
  VelocityTable table = landingTable();
  buildReport({on(990, C4, 100), on(1000, C4, 30), off(1500, C4), off(2000, C4)}, &table);
  assertRoll({{1089, PRESS, C4_KEY, 100}, {2149, RELEASE, C4_KEY, 0}});
  TEST_ASSERT_EQUAL(NOTE_MERGED, report.notes[0].changes);
  TEST_ASSERT_EQUAL_UINT32(1000, report.notes[0].mergedIntoMs);
  TEST_ASSERT_EQUAL_UINT32(990, report.notes[1].playedOnMs);
}


void test_duplicate_note_is_played_once_and_reported_apart()
{
  buildReport({on(1000, C4, 38), on(1000, C4, 38), off(1140, C4), off(1140, C4)});
  assertRoll({{1000, PRESS, C4_KEY, 38}, {1140, RELEASE, C4_KEY, 0}});
  TEST_ASSERT_EQUAL_UINT32(1, roll.duplicateNotes);
  TEST_ASSERT_EQUAL_UINT32(0, roll.mergedNotes);
  TEST_ASSERT_EQUAL_UINT32(1, roll.noteCount);
  TEST_ASSERT_EQUAL(2, report.notes.size());
  TEST_ASSERT_EQUAL(0, report.notes[0].changes); // nothing changed about the note played
  TEST_ASSERT_EQUAL_UINT32(1140, report.notes[0].writtenOffMs);
  TEST_ASSERT_EQUAL(NOTE_DUPLICATE, report.notes[1].changes);
  TEST_ASSERT_EQUAL_UINT32(1000, report.notes[1].mergedIntoMs);
  TEST_ASSERT_EQUAL_UINT32(1140, report.notes[1].writtenOffMs);
}

// landingTable, plus a pedal entry for C4 whose hammer lands 30 ms sooner (120 - velocity, from 0).
static VelocityTable pedalTable()
{
  std::string duties, up, down;
  for (int velocity = 1; velocity <= 127; velocity++)
  {
    duties += (velocity > 1 ? "," : "") + std::string("40");
    up += (velocity > 1 ? "," : "") + std::to_string(150 - velocity);
    down += (velocity > 1 ? "," : "") + std::to_string(std::max(120 - velocity, 0));
  }
  std::string json = "{\"landing_ms\":[" + up + "],\"keys\":{\"" + std::to_string(C4_KEY) + "\":{\"duty\":[" + duties +
                     "],\"pedal\":{\"duty\":[" + duties + "],\"landing_ms\":[" + down + "]}}}}";
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(table.load(json.data(), json.size(), error), error.c_str());
  return table;
}

void test_notes_under_the_pedal_use_its_landing_times()
{
  // Lead 149. C4 at 1000 with the pedal up lands 50 ms after its press; at 2000,
  // with the pedal down since 1800, 20 ms after. Each is sent so it sounds lead
  // after the MIDI.
  VelocityTable table = pedalTable();
  build({on(1000, C4, 100), off(1500, C4), pedal(1800, 127), on(2000, C4, 100), off(2500, C4), pedal(3000, 0)},
        &table);
  std::vector<RollEvent> presses;
  for (const RollEvent &event : roll.events)
  {
    if (event.type == RollEventType::KeyPress)
    {
      presses.push_back(event);
    }
  }
  TEST_ASSERT_EQUAL(2, presses.size());
  TEST_ASSERT_EQUAL_UINT32(1000 + 149 - 50, presses[0].timeMs);
  TEST_ASSERT_FALSE(presses[0].pedal);
  TEST_ASSERT_EQUAL_UINT32(2000 + 149 - 20, presses[1].timeMs);
  TEST_ASSERT_TRUE(presses[1].pedal);
}

void test_pedal_counts_only_once_the_dampers_are_surely_up()
{
  // C4 at velocity 100 is pressed 50 ms before it sounds (with the pedal up),
  // and the pedal needs 100 ms to lift the dampers: down 150 ms before the
  // note, it counts; any later, or lifting before the note sounds, it doesn't.
  VelocityTable table = pedalTable();
  auto pressedWithPedal = []()
  {
    for (const RollEvent &event : roll.events)
    {
      if (event.type == RollEventType::KeyPress)
      {
        return event.pedal;
      }
    }
    TEST_FAIL_MESSAGE("no press");
    return false;
  };
  build({pedal(850, 127), on(1000, C4, 100), off(1500, C4)}, &table);
  TEST_ASSERT_TRUE(pressedWithPedal());
  build({pedal(851, 127), on(1000, C4, 100), off(1500, C4)}, &table);
  TEST_ASSERT_FALSE(pressedWithPedal());
  build({pedal(1000, 127), on(1000, C4, 100), off(1500, C4)}, &table);
  TEST_ASSERT_FALSE(pressedWithPedal());
  build({pedal(0, 127), pedal(999, 0), on(1000, C4, 100), off(1500, C4)}, &table);
  TEST_ASSERT_FALSE(pressedWithPedal());
  build({pedal(0, 127), pedal(1000, 0), on(1000, C4, 100), off(1500, C4)}, &table);
  TEST_ASSERT_TRUE(pressedWithPedal()); // lifts as it sounds: the key's travel is over
}

// --- dynamics ---

static MidiEvent onIn(uint8_t part, uint32_t timeMs, uint8_t note, uint8_t velocity)
{
  return {timeMs, MidiEventType::NoteOn, note, velocity, part};
}

static std::vector<uint8_t> pressVelocities()
{
  std::vector<uint8_t> velocities;
  for (const RollEvent &event : roll.events)
  {
    if (event.type == RollEventType::KeyPress)
    {
      velocities.push_back(event.velocity);
    }
  }
  return velocities;
}

static void buildWith(std::vector<MidiEvent> events, const Dynamics &dynamics)
{
  TEST_ASSERT_TRUE(buildPianoRoll(events, roll, nullptr, dynamics));
}

void test_part_offsets_move_each_part()
{
  Dynamics dynamics;
  dynamics.partOffset[0] = 10;
  dynamics.partOffset[1] = -10;
  buildWith({onIn(0, 0, C4, 60), off(500, C4), onIn(1, 1000, D4, 60), off(1500, D4)}, dynamics);
  std::vector<uint8_t> velocities = pressVelocities();
  TEST_ASSERT_EQUAL_UINT8(70, velocities[0]);
  TEST_ASSERT_EQUAL_UINT8(50, velocities[1]);
}

void test_voicing_stretches_each_chord_around_its_average()
{
  // C4 and D4 start 30 ms apart: one chord (average 70). E4 at 1000 is alone.
  const uint8_t E4 = 64;
  Dynamics dynamics;
  dynamics.voicing = 200;
  buildWith({on(0, C4, 60), on(30, D4, 80), off(500, C4), off(500, D4), on(1000, E4, 90), off(1500, E4)}, dynamics);
  std::vector<uint8_t> velocities = pressVelocities();
  TEST_ASSERT_EQUAL_UINT8(50, velocities[0]);
  TEST_ASSERT_EQUAL_UINT8(90, velocities[1]);
  TEST_ASSERT_EQUAL_UINT8(90, velocities[2]); // unchanged
}

void test_voicing_chords_are_measured_from_their_first_note()
{
  // 0, 30, 60: the third is 60 ms after the first, so it's not in that chord.
  const uint8_t E4 = 64;
  Dynamics dynamics;
  dynamics.voicing = 200;
  buildWith({on(0, C4, 60), on(30, D4, 80), on(60, E4, 100), off(500, C4), off(500, D4), off(500, E4)}, dynamics);
  std::vector<uint8_t> velocities = pressVelocities();
  TEST_ASSERT_EQUAL_UINT8(50, velocities[0]);
  TEST_ASSERT_EQUAL_UINT8(90, velocities[1]);
  TEST_ASSERT_EQUAL_UINT8(100, velocities[2]);
}

void test_contrast_stretches_around_the_median()
{
  const uint8_t E4 = 64;
  Dynamics dynamics;
  dynamics.contrast = 150;
  buildWith({on(0, C4, 40), off(200, C4), on(1000, D4, 60), off(1200, D4), on(2000, E4, 80), off(2200, E4)},
            dynamics);
  std::vector<uint8_t> velocities = pressVelocities();
  TEST_ASSERT_EQUAL_UINT8(30, velocities[0]);
  TEST_ASSERT_EQUAL_UINT8(60, velocities[1]);
  TEST_ASSERT_EQUAL_UINT8(90, velocities[2]);
}

void test_dynamics_apply_in_order_and_round_once()
{
  // Offset +10: 50, 70 (one chord). Voicing 150: 45, 75. Contrast 200 around
  // their median 60: 30, 90. Volume 50: 15, 45.
  Dynamics dynamics;
  dynamics.partOffset[0] = 10;
  dynamics.voicing = 150;
  dynamics.contrast = 200;
  dynamics.volume = 50;
  buildWith({on(0, C4, 40), on(0, D4, 60), off(500, C4), off(500, D4)}, dynamics);
  std::vector<uint8_t> velocities = pressVelocities();
  TEST_ASSERT_EQUAL_UINT8(15, velocities[0]);
  TEST_ASSERT_EQUAL_UINT8(45, velocities[1]);
}

void test_dynamics_stay_within_1_and_127()
{
  Dynamics dynamics;
  dynamics.contrast = 300;
  TEST_ASSERT_TRUE(buildPianoRoll({on(0, C4, 10), off(200, C4), on(1000, D4, 64), off(1200, D4), on(2000, C4, 120),
                                   off(2200, C4)},
                                  roll, nullptr, dynamics, &report));
  TEST_ASSERT_EQUAL_UINT8(1, report.notes[0].playedVelocity);
  TEST_ASSERT_EQUAL(NOTE_VELOCITY_LIMITED, report.notes[0].changes);
  TEST_ASSERT_EQUAL_UINT8(127, report.notes[2].playedVelocity);
  TEST_ASSERT_EQUAL(0, report.notes[1].changes);
  TEST_ASSERT_EQUAL_UINT16(300, roll.dynamics.contrast);
}

void test_report_says_each_notes_part()
{
  buildReport({onIn(0, 0, C4, 60), off(500, C4), onIn(1, 1000, D4, 60), off(1500, D4)});
  TEST_ASSERT_EQUAL_UINT8(0, report.notes[0].part);
  TEST_ASSERT_EQUAL_UINT8(1, report.notes[1].part);
}

void test_repeat_boost_lets_a_soft_repeat_play()
{
  // A repeat at velocity 38, 140 ms after the last, needs 195 ms: merged. With a
  // boost of 40 it plays at 78, which can repeat from 90 ms.
  std::vector<MidiEvent> events = {on(0, C4, 38), off(50, C4), on(140, C4, 38), off(300, C4)};
  buildReport(events);
  TEST_ASSERT_EQUAL_UINT32(1, roll.mergedNotes);
  Dynamics dynamics;
  dynamics.repeatBoost = 40;
  TEST_ASSERT_TRUE(buildPianoRoll(events, roll, nullptr, dynamics, &report));
  TEST_ASSERT_EQUAL_UINT32(0, roll.mergedNotes);
  std::vector<uint8_t> velocities = pressVelocities();
  TEST_ASSERT_EQUAL_UINT8(38, velocities[0]); // the first isn't a repeat
  TEST_ASSERT_EQUAL_UINT8(78, velocities[1]);
  TEST_ASSERT_EQUAL(NOTE_REPEAT_BOOSTED, report.notes[1].changes);
  TEST_ASSERT_EQUAL_UINT8(78, report.notes[1].playedVelocity);
}

void test_repeat_boost_leaves_merged_repeats_alone()
{
  // 60 ms is too soon even boosted: merged, at its own velocity, so the note
  // it's held in stays as loud as it was.
  Dynamics dynamics;
  dynamics.repeatBoost = 40;
  TEST_ASSERT_TRUE(buildPianoRoll({on(0, C4, 60), off(50, C4), on(60, C4, 50), off(300, C4)}, roll, nullptr, dynamics,
                                  &report));
  TEST_ASSERT_EQUAL_UINT32(1, roll.mergedNotes);
  TEST_ASSERT_EQUAL_UINT8(60, pressVelocities()[0]);
  TEST_ASSERT_EQUAL(NOTE_MERGED, report.notes[1].changes);
}

void test_boosted_repeat_is_timed_for_its_louder_landing()
{
  // Landing 150 - velocity, lead 149. C4 at velocity 40, 200 ms after the last:
  // boosted to 80, it lands 70 ms after its press (not 110), so it's pressed
  // 40 ms later, and 10 ms early as a repeat: 1200 + 149 - 70 - 10.
  VelocityTable table = landingTable();
  Dynamics dynamics;
  dynamics.repeatBoost = 40;
  TEST_ASSERT_TRUE(buildPianoRoll({on(1000, C4, 40), off(1100, C4), on(1200, C4, 40), off(1500, C4)}, roll, &table,
                                  dynamics));
  std::vector<RollEvent> presses;
  for (const RollEvent &event : roll.events)
  {
    if (event.type == RollEventType::KeyPress)
    {
      presses.push_back(event);
    }
  }
  TEST_ASSERT_EQUAL(2, presses.size());
  TEST_ASSERT_EQUAL_UINT8(80, presses[1].velocity);
  TEST_ASSERT_EQUAL_UINT32(1200 + 149 - 70 - 10, presses[1].timeMs);
  TEST_ASSERT_TRUE(presses[1].repeat);
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_notes_map_to_keys);
  RUN_TEST(test_notes_outside_piano_are_skipped);
  RUN_TEST(test_unmatched_note_off_is_ignored);
  RUN_TEST(test_note_never_released_ends_with_last_event);
  RUN_TEST(test_short_note_is_lengthened);
  RUN_TEST(test_overlapping_note_waits_for_its_own_note_off);
  RUN_TEST(test_repeat_with_short_gap_is_released_earlier);
  RUN_TEST(test_repeat_with_no_gap_is_released_earlier);
  RUN_TEST(test_repeat_is_sent_early_and_marked);
  RUN_TEST(test_strike_long_after_the_last_is_not_a_repeat);
  RUN_TEST(test_repeat_limits_follow_the_repeats_velocity);
  RUN_TEST(test_repeats_around_88_skip_their_dead_zone);
  RUN_TEST(test_repeat_boost_lets_a_soft_repeat_play);
  RUN_TEST(test_repeat_boost_leaves_merged_repeats_alone);
  RUN_TEST(test_boosted_repeat_is_timed_for_its_louder_landing);
  RUN_TEST(test_fast_repeats_keep_as_many_strikes_as_possible);
  RUN_TEST(test_grace_note_merges_into_main_note_at_louder_velocity);
  RUN_TEST(test_repeats_on_other_keys_are_independent);
  RUN_TEST(test_pedal_threshold_and_repeats);
  RUN_TEST(test_quick_repedal_delays_the_press);
  RUN_TEST(test_brief_pedal_press_is_dropped);
  RUN_TEST(test_pedal_delays_do_not_pile_up);
  RUN_TEST(test_presses_are_sent_early_by_their_landing_time);
  RUN_TEST(test_short_note_is_held_until_after_it_lands);
  RUN_TEST(test_pedal_moves_by_the_lead);
  RUN_TEST(test_soft_repeats_need_time_for_the_hammer_to_land);
  RUN_TEST(test_table_without_landing_times_changes_nothing);
  RUN_TEST(test_volume_scales_velocities);
  RUN_TEST(test_volume_stays_within_1_and_127);
  RUN_TEST(test_volume_times_notes_by_their_new_velocity);
  RUN_TEST(test_same_time_order_is_release_pedal_press);
  RUN_TEST(test_empty_input);
  RUN_TEST(test_builder_resets_previous_contents);
  RUN_TEST(test_start_from_the_beginning);
  RUN_TEST(test_start_part_way_knows_held_keys_and_the_pedal);
  RUN_TEST(test_start_after_the_pedal_is_lifted);
  RUN_TEST(test_start_moves_back_by_the_lead);
  RUN_TEST(test_start_skips_presses_that_sound_before_it);
  RUN_TEST(test_start_at_the_end_plays_nothing);
  RUN_TEST(test_report_unchanged_note);
  RUN_TEST(test_report_merged_repeat);
  RUN_TEST(test_report_shortened_and_lengthened);
  RUN_TEST(test_report_velocity_limited_by_volume);
  RUN_TEST(test_report_is_on_the_midi_timeline);
  RUN_TEST(test_report_pedal_as_written_and_played);
  RUN_TEST(test_doubled_note_is_struck_once_at_the_louder_velocity);
  RUN_TEST(test_louder_note_written_just_before_sounds_on_time);
  RUN_TEST(test_duplicate_note_is_played_once_and_reported_apart);
  RUN_TEST(test_notes_under_the_pedal_use_its_landing_times);
  RUN_TEST(test_pedal_counts_only_once_the_dampers_are_surely_up);
  RUN_TEST(test_part_offsets_move_each_part);
  RUN_TEST(test_voicing_stretches_each_chord_around_its_average);
  RUN_TEST(test_voicing_chords_are_measured_from_their_first_note);
  RUN_TEST(test_contrast_stretches_around_the_median);
  RUN_TEST(test_dynamics_apply_in_order_and_round_once);
  RUN_TEST(test_dynamics_stay_within_1_and_127);
  RUN_TEST(test_report_says_each_notes_part);
  return UNITY_END();
}
