#include <unity.h>
#include <string>
#include "app/piano/live_player.h"
#include "app/piano/velocity_table.h"

void setUp()
{
}

void tearDown()
{
}

static const uint8_t C4 = 60, C4_KEY = 39, D4 = 62, D4_KEY = 41;
static const uint8_t ON = 0x90, OFF = 0x80, CONTROL = 0xB0;

// Hammers land 150 - velocity ms after the press (86 ms at 64).
static VelocityTable &table()
{
  static VelocityTable t;
  static bool loaded = false;
  if (!loaded)
  {
    std::string json = "{\"keys\":{},\"landing_ms\":[";
    for (int v = 1; v <= 127; v++)
    {
      json += (v > 1 ? "," : "") + std::to_string(150 - v);
    }
    json += "]}";
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(t.load(json.data(), json.size(), error), error.c_str());
    loaded = true;
  }
  return t;
}

static LivePlayer live;

static void start(uint16_t delayMs = 200)
{
  live.start(delayMs, PlayConfig(), &table());
}

// Everything scheduled, as "P39@5114 R39@5600 D@... U@...".
static std::string actions()
{
  std::vector<LiveAction> out;
  live.takeDue(UINT32_MAX / 2, out);
  std::string text;
  for (const LiveAction &a : out)
  {
    const char *kind = a.type == LiveAction::Type::Press     ? "P"
                       : a.type == LiveAction::Type::Release ? "R"
                       : a.type == LiveAction::Type::PedalDown ? "D"
                                                             : "U";
    text += (text.empty() ? "" : " ") + std::string(kind) +
            (a.type == LiveAction::Type::Press || a.type == LiveAction::Type::Release ? std::to_string(a.key) : "") +
            "@" + std::to_string(a.atMs) + (a.type == LiveAction::Type::Press && a.pedal ? "p" : "") +
            (a.type == LiveAction::Type::Press && a.repeat ? "r" : "");
  }
  return text;
}

// Sent at sender time ms, arriving networkMs later, on a sender clock 4000 ms behind.
static void send(uint32_t ms, uint8_t status, uint8_t a, uint8_t b, uint32_t networkMs = 0)
{
  live.message(ms, ms + 4000 + networkMs, status, a, b);
}

void test_a_note_sounds_the_delay_after_it_was_played()
{
  start();
  send(1000, ON, C4, 64);
  send(1500, OFF, C4, 0);
  // Sounds at 5200 (1000 + 4000 + 200), pressed 86 ms before.
  TEST_ASSERT_EQUAL_STRING("P39@5114 R39@5700", actions().c_str());
  TEST_ASSERT_EQUAL_UINT32(0, live.late());
}

void test_network_delays_within_the_delay_dont_move_notes()
{
  start();
  send(1000, ON, C4, 64);
  send(1100, ON, D4, 64, 100); // 100 ms slower across
  TEST_ASSERT_EQUAL_STRING("P39@5114 P41@5214", actions().c_str());
  TEST_ASSERT_EQUAL_UINT32(0, live.late());
}

void test_a_note_too_late_for_the_delay_plays_at_once()
{
  start();
  send(1000, ON, C4, 64);
  send(1100, ON, D4, 64, 300);
  TEST_ASSERT_EQUAL_STRING("P39@5114 P41@5400", actions().c_str());
  TEST_ASSERT_EQUAL_UINT32(1, live.late());
}

void test_a_quicker_message_moves_the_clock()
{
  start();
  send(1000, ON, C4, 64, 50); // the first was slow
  send(1100, ON, D4, 64);     // this one quicker: 50 ms earlier than the first's clock
  TEST_ASSERT_EQUAL_STRING("P39@5164 P41@5214", actions().c_str());
}

void test_soft_and_loud_notes_sound_together()
{
  start();
  send(1000, ON, C4, 20); // lands 130 ms after its press
  send(1000, ON, D4, 120); // 30 ms
  TEST_ASSERT_EQUAL_STRING("P39@5070 P41@5170", actions().c_str());
}

void test_a_repeat_too_soon_is_held_in_the_strike_before()
{
  start();
  send(1000, ON, C4, 64);
  send(1050, OFF, C4, 0);
  send(1080, ON, C4, 64);
  send(1300, OFF, C4, 0);
  TEST_ASSERT_EQUAL_STRING("P39@5114 R39@5500", actions().c_str());
}

void test_a_key_is_released_in_time_to_strike_again()
{
  start();
  send(1000, ON, C4, 64);
  send(1450, OFF, C4, 0);
  send(1500, ON, C4, 64); // pressed at 5614: released 70 ms before
  send(1800, OFF, C4, 0);
  TEST_ASSERT_EQUAL_STRING("P39@5114 R39@5544 P39@5614 R39@6000", actions().c_str());
}

void test_a_note_on_a_held_key_ends_the_one_before()
{
  start();
  send(1000, ON, C4, 64);
  send(1500, ON, C4, 64);  // no note-off between
  send(1600, OFF, C4, 0);  // the first note's: it's already ended
  send(1900, OFF, C4, 0);
  TEST_ASSERT_EQUAL_STRING("P39@5114 R39@5544 P39@5614 R39@6100", actions().c_str());
}

void test_notes_after_the_pedal_settles_play_with_it_down()
{
  start();
  send(1000, CONTROL, 64, 127); // down at 5200
  send(1050, ON, C4, 64);       // pressed at 5164: too soon
  send(1500, ON, D4, 64);       // pressed at 5614: settled
  TEST_ASSERT_EQUAL_STRING("P39@5164 D@5200 P41@5614p", actions().c_str());
}

void test_pedal_changes_are_spaced()
{
  start();
  send(1000, CONTROL, 64, 127);
  send(1020, CONTROL, 64, 0); // 100 ms after the last at the earliest
  send(1030, CONTROL, 64, 0); // already up
  TEST_ASSERT_EQUAL_STRING("D@5200 U@5300", actions().c_str());
}

void test_other_messages_and_notes_are_ignored()
{
  start();
  send(1000, ON, 10, 64);         // below the piano
  send(1000, 0xE0, 0, 64);        // pitch bend
  send(1000, CONTROL, 7, 100);    // volume
  send(1000, ON | 3, C4, 0);      // note on at velocity 0: an off, nothing held
  TEST_ASSERT_EQUAL_STRING("", actions().c_str());
}

void test_only_due_actions_are_taken()
{
  start();
  send(1000, ON, C4, 64);
  send(1500, OFF, C4, 0);
  std::vector<LiveAction> out;
  live.takeDue(5113, out);
  TEST_ASSERT_EQUAL(0, out.size());
  uint32_t next = 0;
  TEST_ASSERT_TRUE(live.nextDue(next));
  TEST_ASSERT_EQUAL_UINT32(5114, next);
  live.takeDue(5114, out);
  TEST_ASSERT_EQUAL(1, out.size());
  live.stop();
  TEST_ASSERT_FALSE(live.nextDue(next));
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_a_note_sounds_the_delay_after_it_was_played);
  RUN_TEST(test_network_delays_within_the_delay_dont_move_notes);
  RUN_TEST(test_a_note_too_late_for_the_delay_plays_at_once);
  RUN_TEST(test_a_quicker_message_moves_the_clock);
  RUN_TEST(test_soft_and_loud_notes_sound_together);
  RUN_TEST(test_a_repeat_too_soon_is_held_in_the_strike_before);
  RUN_TEST(test_a_key_is_released_in_time_to_strike_again);
  RUN_TEST(test_a_note_on_a_held_key_ends_the_one_before);
  RUN_TEST(test_notes_after_the_pedal_settles_play_with_it_down);
  RUN_TEST(test_pedal_changes_are_spaced);
  RUN_TEST(test_other_messages_and_notes_are_ignored);
  RUN_TEST(test_only_due_actions_are_taken);
  return UNITY_END();
}
