#include <unity.h>
#include "app/piano/roll_builder.h"

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

static void build(std::vector<MidiEvent> events)
{
  TEST_ASSERT_TRUE(buildPianoRoll(events, roll));
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
  build({on(0, C4), off(10, C4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {40, RELEASE, C4_KEY, 0}});
}

// --- repeated notes on one key ---

void test_overlapping_note_waits_for_its_own_note_off()
{
  // Two hands on one key: B's strike ends A, and A's note-off must not end B.
  build({on(0, C4, 50), on(500, C4, 60), off(700, C4), off(1000, C4)});
  assertRoll({{0, PRESS, C4_KEY, 50}, {420, RELEASE, C4_KEY, 0}, {500, PRESS, C4_KEY, 60}, {1000, RELEASE, C4_KEY, 0}});
}

void test_repeat_with_short_gap_is_released_earlier()
{
  build({on(0, C4), off(500, C4), on(520, C4), off(800, C4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {440, RELEASE, C4_KEY, 0}, {520, PRESS, C4_KEY, 64}, {800, RELEASE, C4_KEY, 0}});
}

void test_repeat_with_no_gap_is_released_earlier()
{
  build({on(0, C4), off(300, C4), on(300, C4), off(600, C4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {220, RELEASE, C4_KEY, 0}, {300, PRESS, C4_KEY, 64}, {600, RELEASE, C4_KEY, 0}});
}

void test_repeat_with_enough_gap_is_unchanged()
{
  build({on(0, C4), off(100, C4), on(200, C4), off(300, C4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {100, RELEASE, C4_KEY, 0}, {200, PRESS, C4_KEY, 64}, {300, RELEASE, C4_KEY, 0}});
  TEST_ASSERT_EQUAL_UINT32(0, roll.mergedNotes);
}

void test_fast_repeats_keep_as_many_strikes_as_possible()
{
  // 4 strikes 60 ms apart: a key can strike every 120 ms, so every other one plays.
  build({on(0, C4), off(50, C4), on(60, C4), off(110, C4), on(120, C4), off(170, C4), on(180, C4), off(230, C4)});
  assertRoll({{0, PRESS, C4_KEY, 64}, {40, RELEASE, C4_KEY, 0}, {120, PRESS, C4_KEY, 64}, {230, RELEASE, C4_KEY, 0}});
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
  assertRoll({{0, PRESS, C4_KEY, 64}, {10, PRESS, D4_KEY, 64}, {40, RELEASE, C4_KEY, 0}, {50, RELEASE, D4_KEY, 0}});
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
  RUN_TEST(test_repeat_with_enough_gap_is_unchanged);
  RUN_TEST(test_fast_repeats_keep_as_many_strikes_as_possible);
  RUN_TEST(test_grace_note_merges_into_main_note_at_louder_velocity);
  RUN_TEST(test_repeats_on_other_keys_are_independent);
  RUN_TEST(test_pedal_threshold_and_repeats);
  RUN_TEST(test_quick_repedal_delays_the_press);
  RUN_TEST(test_brief_pedal_press_is_dropped);
  RUN_TEST(test_pedal_delays_do_not_pile_up);
  RUN_TEST(test_same_time_order_is_release_pedal_press);
  RUN_TEST(test_empty_input);
  RUN_TEST(test_builder_resets_previous_contents);
  return UNITY_END();
}
