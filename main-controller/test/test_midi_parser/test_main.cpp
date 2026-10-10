#include <unity.h>
#include "app/midi/midi_parser.h"
#include "../helpers/midi_builder.h"

static std::vector<MidiEvent> events;
static std::string error;

void setUp()
{
  events.clear();
  error.clear();
}

void tearDown()
{
}

static bool parse(const Bytes &file)
{
  return parseMidiFile(file.data(), file.size(), events, error);
}

static void assertEvent(size_t index, uint32_t timeMs, MidiEventType type, uint8_t note, uint8_t value)
{
  char message[48];
  snprintf(message, sizeof(message), "event %u", (unsigned)index);
  TEST_ASSERT_TRUE_MESSAGE(index < events.size(), message);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(timeMs, events[index].timeMs, message);
  TEST_ASSERT_EQUAL_INT_MESSAGE((int)type, (int)events[index].type, message);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(note, events[index].note, message);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(value, events[index].value, message);
}

static void assertError(const char *expected)
{
  TEST_ASSERT_EQUAL_STRING(expected, error.c_str());
  TEST_ASSERT_EQUAL(0, events.size());
}

// --- timing ---

void test_ticks_convert_to_ms_at_default_tempo()
{
  // 480 ticks per quarter at 120 BPM: 480 ticks = 500 ms.
  Bytes file = midiFile(0, 480, {TrackBuilder().noteOn(0, 60, 64).noteOff(480, 60).build()});
  TEST_ASSERT_TRUE(parse(file));
  TEST_ASSERT_EQUAL(2, events.size());
  assertEvent(0, 0, MidiEventType::NoteOn, 60, 64);
  assertEvent(1, 500, MidiEventType::NoteOff, 60, 0);
}

void test_tempo_change_in_conductor_track_applies_to_other_tracks()
{
  // Tempo doubles at tick 960 (1000 ms): the next 480 ticks take 250 ms.
  Bytes conductor = TrackBuilder().tempo(0, 500000).tempo(960, 250000).build();
  Bytes notes = TrackBuilder().noteOn(960, 60, 64).noteOff(1440, 60).build();
  TEST_ASSERT_TRUE(parse(midiFile(1, 480, {conductor, notes})));
  assertEvent(0, 1000, MidiEventType::NoteOn, 60, 64);
  assertEvent(1, 1250, MidiEventType::NoteOff, 60, 0);
}

void test_smpte_division()
{
  Bytes file = midiFile(0, MS_DIVISION, {TrackBuilder().noteOn(0, 60, 64).noteOff(1000, 60).build()});
  TEST_ASSERT_TRUE(parse(file));
  assertEvent(1, 1000, MidiEventType::NoteOff, 60, 0);
}

void test_smpte_ignores_tempo_events()
{
  Bytes file = midiFile(0, MS_DIVISION, {TrackBuilder().tempo(0, 250000).noteOn(0, 60, 64).noteOff(1000, 60).build()});
  TEST_ASSERT_TRUE(parse(file));
  assertEvent(1, 1000, MidiEventType::NoteOff, 60, 0);
}

// --- events ---

void test_running_status()
{
  // The second and third events leave out the 0x90 status byte.
  Bytes track = TrackBuilder().at(0, {0x90, 60, 64}).at(0, {62, 70}).at(100, {60, 0}).build();
  TEST_ASSERT_TRUE(parse(midiFile(0, MS_DIVISION, {track})));
  TEST_ASSERT_EQUAL(3, events.size());
  assertEvent(0, 0, MidiEventType::NoteOn, 60, 64);
  assertEvent(1, 0, MidiEventType::NoteOn, 62, 70);
  assertEvent(2, 100, MidiEventType::NoteOff, 60, 0);
}

void test_note_on_with_velocity_zero_is_note_off()
{
  Bytes track = TrackBuilder().noteOn(0, 60, 64).noteOn(100, 60, 0).build();
  TEST_ASSERT_TRUE(parse(midiFile(0, MS_DIVISION, {track})));
  assertEvent(1, 100, MidiEventType::NoteOff, 60, 0);
}

void test_sustain_value_passed_through()
{
  Bytes track = TrackBuilder().sustain(0, 100).sustain(50, 10).build();
  TEST_ASSERT_TRUE(parse(midiFile(0, MS_DIVISION, {track})));
  assertEvent(0, 0, MidiEventType::Sustain, 0, 100);
  assertEvent(1, 50, MidiEventType::Sustain, 0, 10);
}

void test_drum_channel_and_other_messages_skipped()
{
  Bytes track = TrackBuilder()
                    .noteOn(0, 36, 100, 9)        // drums
                    .at(0, {0xC0, 5})             // program change
                    .at(0, {0xE0, 0x00, 0x40})    // pitch bend
                    .at(0, {0xB0, 7, 100})        // volume controller
                    .at(0, {0xFF, 0x03, 2, 'h', 'i'}) // track name
                    .at(0, {0xF0, 2, 0x01, 0xF7}) // sysex
                    .noteOn(10, 60, 64)
                    .build();
  TEST_ASSERT_TRUE(parse(midiFile(0, MS_DIVISION, {track})));
  TEST_ASSERT_EQUAL(1, events.size());
  assertEvent(0, 10, MidiEventType::NoteOn, 60, 64);
}

void test_same_time_order_is_note_off_sustain_note_on()
{
  Bytes track = TrackBuilder().noteOn(0, 60, 64).noteOn(100, 62, 70).sustain(100, 127).noteOff(100, 60).build();
  TEST_ASSERT_TRUE(parse(midiFile(0, MS_DIVISION, {track})));
  assertEvent(1, 100, MidiEventType::NoteOff, 60, 0);
  assertEvent(2, 100, MidiEventType::Sustain, 0, 127);
  assertEvent(3, 100, MidiEventType::NoteOn, 62, 70);
}

void test_tracks_are_merged_by_time()
{
  Bytes right = TrackBuilder().noteOn(100, 72, 64).build();
  Bytes left = TrackBuilder().noteOn(50, 48, 64).noteOn(150, 50, 64).build();
  TEST_ASSERT_TRUE(parse(midiFile(1, MS_DIVISION, {right, left})));
  assertEvent(0, 50, MidiEventType::NoteOn, 48, 64);
  assertEvent(1, 100, MidiEventType::NoteOn, 72, 64);
  assertEvent(2, 150, MidiEventType::NoteOn, 50, 64);
}

void test_missing_end_of_track_keeps_events()
{
  Bytes track = TrackBuilder().noteOn(0, 60, 64).noteOff(100, 60).build(false);
  TEST_ASSERT_TRUE(parse(midiFile(0, MS_DIVISION, {track})));
  TEST_ASSERT_EQUAL(2, events.size());
}

void test_unknown_chunks_are_skipped()
{
  Bytes file = midiFile(0, MS_DIVISION, {});
  file[11] = 1; // track count
  Bytes unknown = {'X', 'Y', 'Z', 'W', 0, 0, 0, 2, 0xAA, 0xBB};
  Bytes track = TrackBuilder().noteOn(0, 60, 64).build();
  file.insert(file.end(), unknown.begin(), unknown.end());
  file.insert(file.end(), track.begin(), track.end());
  TEST_ASSERT_TRUE(parse(file));
  TEST_ASSERT_EQUAL(1, events.size());
}

// --- errors ---

void test_rejects_non_midi()
{
  const char text[] = "hello, not a MIDI file";
  TEST_ASSERT_FALSE(parseMidiFile((const uint8_t *)text, sizeof(text), events, error));
  assertError("not a MIDI file");
}

void test_rejects_empty()
{
  TEST_ASSERT_FALSE(parse(Bytes()));
  assertError("not a MIDI file");
}

void test_rejects_null()
{
  TEST_ASSERT_FALSE(parseMidiFile(nullptr, 10, events, error));
  assertError("no data");
}

void test_rejects_truncated_file()
{
  Bytes file = midiFile(0, 480, {TrackBuilder().noteOn(0, 60, 64).noteOff(480, 60).build()});
  file.resize(file.size() - 5);
  TEST_ASSERT_FALSE(parse(file));
  assertError("truncated file");
}

void test_rejects_truncated_event_inside_track()
{
  // The track's length is honest, but its last event is cut short.
  Bytes track = {'M', 'T', 'r', 'k', 0, 0, 0, 3, 0x00, 0x90, 60};
  Bytes file = midiFile(0, 480, {track});
  TEST_ASSERT_FALSE(parse(file));
  assertError("track 0: truncated channel event");
}

void test_rejects_overlong_variable_length()
{
  Bytes track = {'M', 'T', 'r', 'k', 0, 0, 0, 5, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
  TEST_ASSERT_FALSE(parse(midiFile(0, 480, {track})));
  assertError("track 0: truncated track");
}

void test_rejects_data_byte_without_status()
{
  Bytes track = {'M', 'T', 'r', 'k', 0, 0, 0, 3, 0x00, 60, 64};
  TEST_ASSERT_FALSE(parse(midiFile(0, 480, {track})));
  assertError("track 0: data byte without a status byte");
}

void test_rejects_format_2()
{
  TEST_ASSERT_FALSE(parse(midiFile(2, 480, {TrackBuilder().build()})));
  assertError("MIDI format 2 is not supported");
}

void test_rejects_zero_division()
{
  TEST_ASSERT_FALSE(parse(midiFile(0, 0, {TrackBuilder().build()})));
  assertError("invalid time division");
}

void test_rejects_unknown_smpte_rate()
{
  uint16_t division = (uint16_t)((0x100 - 20) << 8 | 40); // 20 fps
  TEST_ASSERT_FALSE(parse(midiFile(0, division, {TrackBuilder().build()})));
  assertError("invalid time division");
}

void test_rejects_file_without_tracks()
{
  TEST_ASSERT_FALSE(parse(midiFile(0, 480, {})));
  assertError("no tracks");
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_ticks_convert_to_ms_at_default_tempo);
  RUN_TEST(test_tempo_change_in_conductor_track_applies_to_other_tracks);
  RUN_TEST(test_smpte_division);
  RUN_TEST(test_smpte_ignores_tempo_events);
  RUN_TEST(test_running_status);
  RUN_TEST(test_note_on_with_velocity_zero_is_note_off);
  RUN_TEST(test_sustain_value_passed_through);
  RUN_TEST(test_drum_channel_and_other_messages_skipped);
  RUN_TEST(test_same_time_order_is_note_off_sustain_note_on);
  RUN_TEST(test_tracks_are_merged_by_time);
  RUN_TEST(test_missing_end_of_track_keeps_events);
  RUN_TEST(test_unknown_chunks_are_skipped);
  RUN_TEST(test_rejects_non_midi);
  RUN_TEST(test_rejects_empty);
  RUN_TEST(test_rejects_null);
  RUN_TEST(test_rejects_truncated_file);
  RUN_TEST(test_rejects_truncated_event_inside_track);
  RUN_TEST(test_rejects_overlong_variable_length);
  RUN_TEST(test_rejects_data_byte_without_status);
  RUN_TEST(test_rejects_format_2);
  RUN_TEST(test_rejects_zero_division);
  RUN_TEST(test_rejects_unknown_smpte_rate);
  RUN_TEST(test_rejects_file_without_tracks);
  return UNITY_END();
}
