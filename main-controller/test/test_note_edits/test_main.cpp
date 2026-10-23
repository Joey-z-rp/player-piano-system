#include <unity.h>
#include <string>
#include "app/piano/note_edits.h"
#include "app/piano/roll_builder.h"
#include "app/piano/velocity_table.h"

void setUp()
{
}

void tearDown()
{
}

static const uint8_t C4 = 60, C4_KEY = 39, D4 = 62, D4_KEY = 41;

// Notes only, as a list: {"notes": list}.
static NoteEdits edits(const std::string &list)
{
  std::string json = "{\"notes\":" + list + "}";
  NoteEdits out;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(readNoteEdits(json.data(), json.size(), out, error), error.c_str());
  return out;
}

void test_edits_are_kept_in_order_one_per_note()
{
  // Out of order; key 39 at 1000 twice (the last counts); one that changes nothing.
  NoteEdits e = edits("[[41,2000,5,0,0],[39,1000,10,0,0],[39,500,0,0,0],[39,1000,-3,100,-20],[40,1000,0,50,0]]");
  TEST_ASSERT_EQUAL(3, e.edits.size());
  TEST_ASSERT_EQUAL_STRING("{\"notes\":[[39,1000,-3,100,-20],[40,1000,0,50,0],[41,2000,5,0,0]],\"pedal\":[]}",
                           noteEditsJson(e).c_str());
  TEST_ASSERT_NOT_NULL(e.find(40, 1000));
  TEST_ASSERT_EQUAL(-20, e.find(39, 1000)->shiftMs);
  TEST_ASSERT_NULL(e.find(39, 500));
  TEST_ASSERT_NULL(e.find(41, 1000));
  TEST_ASSERT_EQUAL(0, edits("[]").edits.size());
}

void test_bad_edits_change_nothing()
{
  NoteEdits e = edits("[[39,1000,10,0,0]]");
  std::string error;
  for (const char *json : {"[[88,0,1,0,0]]", "[[39,0,127,0,0]]", "[[39,0,0,5001,0]]", "[[39,0,0,0,-1001]]",
                           "[[39,-1,1,0,0]]", "[[39,0,1,0]]", "[[39,0,1.5,0,0]]", "{}"})
  {
    std::string body = std::string("{\"notes\":") + json + "}";
    TEST_ASSERT_FALSE_MESSAGE(readNoteEdits(body.data(), body.size(), e, error), json);
  }
  TEST_ASSERT_EQUAL(1, e.edits.size());
}

static PianoRoll roll;
static RollReport report;

static MidiEvent on(uint32_t timeMs, uint8_t note, uint8_t velocity = 64)
{
  return {timeMs, MidiEventType::NoteOn, note, velocity, 0};
}

static MidiEvent off(uint32_t timeMs, uint8_t note)
{
  return {timeMs, MidiEventType::NoteOff, note, 0, 0};
}

static void buildWith(std::vector<MidiEvent> events, const NoteEdits &e, Dynamics dynamics = Dynamics())
{
  TEST_ASSERT_TRUE(buildPianoRoll(events, roll, nullptr, dynamics, &report, nullptr, &e));
}

static void build(std::vector<MidiEvent> events, const std::string &json, Dynamics dynamics = Dynamics())
{
  NoteEdits e = edits(json);
  TEST_ASSERT_TRUE(buildPianoRoll(events, roll, nullptr, dynamics, &report, nullptr, &e));
}

static const NoteReport &noteOn(uint8_t key)
{
  for (const NoteReport &note : report.notes)
  {
    if (note.key == key)
    {
      return note;
    }
  }
  TEST_FAIL_MESSAGE("no such note");
  return report.notes[0];
}

void test_a_velocity_change_comes_before_the_dynamics()
{
  Dynamics half;
  half.volume = 50;
  build({on(0, C4, 64), off(500, C4), on(0, D4, 64), off(500, D4)}, "[[39,0,10,0,0]]", half);
  TEST_ASSERT_EQUAL(37, noteOn(C4_KEY).playedVelocity); // (64 + 10) * 50%
  TEST_ASSERT_EQUAL(NOTE_EDITED, noteOn(C4_KEY).changes);
  TEST_ASSERT_EQUAL(32, noteOn(D4_KEY).playedVelocity);
  TEST_ASSERT_EQUAL(0, noteOn(D4_KEY).changes);
  build({on(0, C4, 120), off(500, C4)}, "[[39,0,20,0,0]]");
  TEST_ASSERT_EQUAL(127, noteOn(C4_KEY).playedVelocity);
  build({on(0, C4, 10), off(500, C4)}, "[[39,0,-20,0,0]]");
  TEST_ASSERT_EQUAL(1, noteOn(C4_KEY).playedVelocity);
}

void test_a_length_change_moves_the_release_and_isnt_a_builder_change()
{
  build({on(0, C4), off(500, C4)}, "[[39,0,0,200,0]]");
  TEST_ASSERT_EQUAL_UINT32(700, noteOn(C4_KEY).playedOffMs);
  TEST_ASSERT_EQUAL_UINT32(500, noteOn(C4_KEY).writtenOffMs);
  TEST_ASSERT_EQUAL(NOTE_EDITED, noteOn(C4_KEY).changes);
  build({on(0, C4), off(500, C4)}, "[[39,0,0,-300,0]]");
  TEST_ASSERT_EQUAL_UINT32(200, noteOn(C4_KEY).playedOffMs);
  TEST_ASSERT_EQUAL(NOTE_EDITED, noteOn(C4_KEY).changes);
  // Never before it's pressed (then the minimum length applies).
  build({on(0, C4), off(500, C4)}, "[[39,0,0,-5000,0]]");
  TEST_ASSERT_TRUE(noteOn(C4_KEY).playedOffMs > 0);
}

void test_a_shift_moves_the_note_and_its_release()
{
  build({on(1000, C4), off(1500, C4)}, "[[39,1000,0,0,-100]]");
  TEST_ASSERT_EQUAL_UINT32(1000, noteOn(C4_KEY).onMs); // as written
  TEST_ASSERT_EQUAL_UINT32(900, noteOn(C4_KEY).playedOnMs);
  TEST_ASSERT_EQUAL_UINT32(1400, noteOn(C4_KEY).playedOffMs);
  build({on(50, C4), off(500, C4)}, "[[39,50,0,0,-100]]");
  TEST_ASSERT_EQUAL_UINT32(0, noteOn(C4_KEY).playedOnMs); // not before the start
}

void test_edits_name_the_note_as_written_and_scale_with_the_speed()
{
  Dynamics slow;
  slow.speed = 50;
  build({on(1000, C4), off(1500, C4)}, "[[39,1000,0,100,-100]]", slow);
  const NoteReport &note = noteOn(C4_KEY);
  TEST_ASSERT_EQUAL_UINT32(2000, note.onMs);
  TEST_ASSERT_EQUAL_UINT32(1000, note.sourceMs);
  TEST_ASSERT_EQUAL_UINT32(1800, note.playedOnMs);
  TEST_ASSERT_EQUAL_UINT32(3000, note.playedOffMs); // 1000 ms at half speed, +200, -200
}

void test_a_note_moved_onto_the_one_before_is_merged_like_a_repeat()
{
  build({on(0, C4, 80), off(500, C4), on(500, C4, 80), off(1000, C4)}, "[[39,500,0,0,-450]]");
  TEST_ASSERT_EQUAL(1, roll.mergedNotes);
  TEST_ASSERT_TRUE(report.notes[1].changes & NOTE_MERGED);
  TEST_ASSERT_TRUE(report.notes[1].changes & NOTE_EDITED);
}

void test_doubled_notes_take_the_edit_of_the_note_they_share()
{
  // Two voices on C4 at once: struck once, louder, then the edit.
  build({on(0, C4, 60), on(0, C4, 70), off(500, C4), off(500, C4)}, "[[39,0,5,0,0]]");
  TEST_ASSERT_EQUAL(1, roll.noteCount);
  TEST_ASSERT_EQUAL(75, noteOn(C4_KEY).playedVelocity);
}

static MidiEvent pedal(uint32_t timeMs, uint8_t value)
{
  return {timeMs, MidiEventType::Sustain, 0, value, 0};
}

static NoteEdits pedalEdits(const std::string &list)
{
  std::string json = "{\"pedal\":" + list + "}";
  NoteEdits out;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(readNoteEdits(json.data(), json.size(), out, error), error.c_str());
  return out;
}

void test_pedal_edits_are_kept_in_order_one_per_press()
{
  NoteEdits e = pedalEdits("[[2000,0,100,0],[1000,-50,0,0],[1000,0,0,1],[3000,0,0,0]]");
  TEST_ASSERT_EQUAL(2, e.pedal.size());
  TEST_ASSERT_TRUE(e.findPedal(1000)->removed);
  TEST_ASSERT_NULL(e.findPedal(3000));
  TEST_ASSERT_EQUAL_STRING("{\"notes\":[],\"pedal\":[[1000,0,0,1],[2000,0,100,0]]}", noteEditsJson(e).c_str());
  std::string error;
  for (const char *json : {"{\"pedal\":[[0,2001,0,0]]}", "{\"pedal\":[[0,0,0,2]]}", "{\"pedal\":[[0,0,0]]}",
                           "{\"notes\":{}}", "[]"})
  {
    std::string body = json;
    TEST_ASSERT_FALSE_MESSAGE(readNoteEdits(body.data(), body.size(), e, error), json);
  }
  TEST_ASSERT_EQUAL(2, e.pedal.size());
}

// The pedal as played, as [down, up] spans (no table, so no lead).
static std::string playedPedal()
{
  std::string out;
  for (const PedalSpan &span : report.playedPedal)
  {
    out += "[" + std::to_string(span.downMs) + "," + std::to_string(span.upMs) + "]";
  }
  return out;
}

void test_a_pedal_press_can_be_moved_or_left_out()
{
  std::vector<MidiEvent> events = {pedal(1000, 127), on(1100, C4), off(1500, C4), pedal(2000, 0),
                                   pedal(3000, 127), pedal(4000, 0), on(4100, D4), off(4200, D4)};
  buildWith(events, pedalEdits("[[1000,-200,300,0]]"));
  TEST_ASSERT_EQUAL_STRING("[800,2300][3000,4000]", playedPedal().c_str());
  TEST_ASSERT_EQUAL_UINT32(1000, report.writtenPedal[0].downMs); // as written
  TEST_ASSERT_EQUAL_UINT32(1000, report.writtenPedal[0].sourceMs);
  TEST_ASSERT_EQUAL_UINT32(3000, report.writtenPedal[1].sourceMs);
  buildWith(events, pedalEdits("[[3000,0,0,1]]"));
  TEST_ASSERT_EQUAL_STRING("[1000,2000]", playedPedal().c_str());
  // Moved onto the next press: one press.
  buildWith(events, pedalEdits("[[1000,0,1500,0]]"));
  TEST_ASSERT_EQUAL_STRING("[1000,4000]", playedPedal().c_str());
}

void test_pedal_edits_scale_with_the_speed()
{
  Dynamics slow;
  slow.speed = 50;
  buildWith({pedal(1000, 127), on(1100, C4), off(1500, C4), pedal(2000, 0)}, pedalEdits("[[1000,-100,100,0]]"), slow);
  TEST_ASSERT_EQUAL_STRING("[1800,4200]", playedPedal().c_str());
  TEST_ASSERT_EQUAL_UINT32(1000, report.writtenPedal[0].sourceMs);
}

void test_a_press_never_lifted_stays_down_when_moved()
{
  buildWith({on(0, C4), off(500, C4), pedal(1000, 127), on(1100, D4), off(1500, D4)}, pedalEdits("[[1000,-500,0,0]]"));
  TEST_ASSERT_EQUAL(1, report.playedPedal.size());
  TEST_ASSERT_EQUAL_UINT32(500, report.playedPedal[0].downMs);
  TEST_ASSERT_EQUAL_UINT32(1500, report.playedPedal[0].upMs); // to the end
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_edits_are_kept_in_order_one_per_note);
  RUN_TEST(test_bad_edits_change_nothing);
  RUN_TEST(test_a_velocity_change_comes_before_the_dynamics);
  RUN_TEST(test_a_length_change_moves_the_release_and_isnt_a_builder_change);
  RUN_TEST(test_a_shift_moves_the_note_and_its_release);
  RUN_TEST(test_edits_name_the_note_as_written_and_scale_with_the_speed);
  RUN_TEST(test_a_note_moved_onto_the_one_before_is_merged_like_a_repeat);
  RUN_TEST(test_doubled_notes_take_the_edit_of_the_note_they_share);
  RUN_TEST(test_pedal_edits_are_kept_in_order_one_per_press);
  RUN_TEST(test_a_pedal_press_can_be_moved_or_left_out);
  RUN_TEST(test_pedal_edits_scale_with_the_speed);
  RUN_TEST(test_a_press_never_lifted_stays_down_when_moved);
  return UNITY_END();
}
