// End-to-end checks on a real, uncleaned piece: Chopin's Nocturne Op. 9
// No. 2 (format 1, 4 tracks, 536 tempo changes, sysex, 1298 notes).

#include <unity.h>
#include <stdio.h>
#include "app/midi/midi_parser.h"
#include "app/piano/roll_builder.h"

static const char FIXTURE[] = "test/fixtures/chopin_nocturne.mid";

static const size_t NOTE_ONS = 1298;
static const size_t NOTE_OFFS = 1298;
static const size_t SUSTAIN_MESSAGES = 268;
static const size_t PEDAL_CHANGES = 228;
static const uint32_t LAST_EVENT_MS = 240637;
static const uint32_t RESTRIKES_UNDER_120_MS = 2;

// The hardware limits the roll builder must respect.
static const uint32_t MIN_NOTE_MS = 40;
static const uint32_t RESTRIKE_GAP_MS = 80;
static const uint32_t PEDAL_MIN_MS = 100;

static std::vector<uint8_t> file;
static std::vector<MidiEvent> events;
static PianoRoll roll;
static bool loaded = false;
static bool built = false;
static std::string error;

// Every test needs the fixture; a missing or unparseable file fails each one.
void setUp()
{
  TEST_ASSERT_TRUE_MESSAGE(loaded, "cannot open test/fixtures/chopin_nocturne.mid (run from the project folder)");
  TEST_ASSERT_TRUE_MESSAGE(built, error.c_str());
}

void tearDown()
{
}

static bool loadFixture()
{
  FILE *f = fopen(FIXTURE, "rb");
  if (f == nullptr)
  {
    return false;
  }
  int c;
  while ((c = fgetc(f)) != EOF)
  {
    file.push_back((uint8_t)c);
  }
  fclose(f);
  return true;
}

static uint8_t order(RollEventType type)
{
  return type == RollEventType::KeyRelease ? 0 : type == RollEventType::KeyPress ? 2
                                                                                 : 1;
}

void test_parser_reads_every_event()
{
  size_t noteOns = 0, noteOffs = 0, sustain = 0;
  for (size_t i = 0; i < events.size(); i++)
  {
    if (i > 0)
    {
      TEST_ASSERT_TRUE_MESSAGE(events[i - 1].timeMs <= events[i].timeMs, "events out of time order");
    }
    switch (events[i].type)
    {
    case MidiEventType::NoteOn:
      noteOns++;
      break;
    case MidiEventType::NoteOff:
      noteOffs++;
      break;
    case MidiEventType::Sustain:
      sustain++;
      break;
    }
  }
  TEST_ASSERT_EQUAL(NOTE_ONS, noteOns);
  TEST_ASSERT_EQUAL(NOTE_OFFS, noteOffs);
  TEST_ASSERT_EQUAL(SUSTAIN_MESSAGES, sustain);
  // 536 tempo changes: the timeline must still land within a couple of ms.
  TEST_ASSERT_UINT32_WITHIN(2, LAST_EVENT_MS, events.back().timeMs);
}

void test_roll_accounts_for_every_note()
{
  TEST_ASSERT_EQUAL_UINT32(0, roll.skippedNotes);
  TEST_ASSERT_EQUAL_UINT32(RESTRIKES_UNDER_120_MS, roll.mergedNotes);
  TEST_ASSERT_EQUAL_UINT32(NOTE_ONS, roll.noteCount + roll.mergedNotes);
  TEST_ASSERT_UINT32_WITHIN(MIN_NOTE_MS, LAST_EVENT_MS, roll.durationMs);
}

void test_roll_is_in_time_order_with_releases_first()
{
  for (size_t i = 1; i < roll.events.size(); i++)
  {
    const RollEvent &a = roll.events[i - 1];
    const RollEvent &b = roll.events[i];
    TEST_ASSERT_TRUE_MESSAGE(a.timeMs <= b.timeMs, "roll out of time order");
    if (a.timeMs == b.timeMs)
    {
      TEST_ASSERT_TRUE_MESSAGE(order(a.type) <= order(b.type), "same-time events not release, pedal, press");
    }
  }
}

void test_every_key_is_released_and_rested_before_striking_again()
{
  bool down[PIANO_KEY_COUNT] = {};
  uint32_t pressedAt[PIANO_KEY_COUNT] = {};
  uint32_t releasedAt[PIANO_KEY_COUNT] = {};
  bool struck[PIANO_KEY_COUNT] = {};
  char message[64];

  for (const RollEvent &event : roll.events)
  {
    uint8_t key = event.key;
    if (event.type == RollEventType::KeyPress)
    {
      snprintf(message, sizeof(message), "key %u pressed at %u while down", key, (unsigned)event.timeMs);
      TEST_ASSERT_FALSE_MESSAGE(down[key], message);
      if (struck[key])
      {
        snprintf(message, sizeof(message), "key %u re-struck at %u too soon", key, (unsigned)event.timeMs);
        TEST_ASSERT_TRUE_MESSAGE(event.timeMs >= releasedAt[key] + RESTRIKE_GAP_MS, message);
      }
      TEST_ASSERT_TRUE(event.velocity >= 1 && event.velocity <= 127);
      down[key] = true;
      struck[key] = true;
      pressedAt[key] = event.timeMs;
    }
    else if (event.type == RollEventType::KeyRelease)
    {
      snprintf(message, sizeof(message), "key %u released at %u while up", key, (unsigned)event.timeMs);
      TEST_ASSERT_TRUE_MESSAGE(down[key], message);
      snprintf(message, sizeof(message), "key %u held under %u ms at %u", key, (unsigned)MIN_NOTE_MS,
               (unsigned)event.timeMs);
      TEST_ASSERT_TRUE_MESSAGE(event.timeMs >= pressedAt[key] + MIN_NOTE_MS, message);
      down[key] = false;
      releasedAt[key] = event.timeMs;
    }
  }

  for (uint8_t key = 0; key < PIANO_KEY_COUNT; key++)
  {
    snprintf(message, sizeof(message), "key %u still down at the end", key);
    TEST_ASSERT_FALSE_MESSAGE(down[key], message);
  }
}

void test_pedal_alternates_and_is_spaced()
{
  bool pedalDown = false;
  bool first = true;
  uint32_t lastChange = 0;
  size_t changes = 0;
  char message[64];

  for (const RollEvent &event : roll.events)
  {
    bool isDown = event.type == RollEventType::SustainOn;
    if (!isDown && event.type != RollEventType::SustainOff)
    {
      continue;
    }
    snprintf(message, sizeof(message), "pedal change at %u repeats the state", (unsigned)event.timeMs);
    TEST_ASSERT_TRUE_MESSAGE(isDown != pedalDown, message);
    if (!first)
    {
      snprintf(message, sizeof(message), "pedal change at %u too soon", (unsigned)event.timeMs);
      TEST_ASSERT_TRUE_MESSAGE(event.timeMs >= lastChange + PEDAL_MIN_MS, message);
    }
    pedalDown = isDown;
    lastChange = event.timeMs;
    first = false;
    changes++;
  }

  TEST_ASSERT_FALSE_MESSAGE(pedalDown, "pedal still down at the end");
  // Spacing can drop brief presses but never adds changes.
  TEST_ASSERT_TRUE(changes > 0 && changes <= PEDAL_CHANGES);
}

int main()
{
  loaded = loadFixture();
  built = loaded && parseMidiFile(file.data(), file.size(), events, error) && buildPianoRoll(events, roll);

  UNITY_BEGIN();
  RUN_TEST(test_parser_reads_every_event);
  RUN_TEST(test_roll_accounts_for_every_note);
  RUN_TEST(test_roll_is_in_time_order_with_releases_first);
  RUN_TEST(test_every_key_is_released_and_rested_before_striking_again);
  RUN_TEST(test_pedal_alternates_and_is_spaced);
  return UNITY_END();
}
