#include <unity.h>
#include <stdio.h>
#include <string>
#include "app/piano/velocity_table.h"

static const char GENERATED_TABLE[] = "data/velocity_table.json";

void setUp()
{
}

void tearDown()
{
}

// A table JSON with the given keys, each key's duty rising from `start` by
// one every `every` velocities.
static std::string tableJson(std::initializer_list<int> keys, int start = 30, int every = 4, int count = 127)
{
  std::string json = "{\"generated\":\"test\",\"keys\":{";
  bool firstKey = true;
  for (int key : keys)
  {
    json += (firstKey ? "\"" : ",\"") + std::to_string(key) + "\":{\"min\":30,\"duty\":[";
    for (int i = 0; i < count; i++)
    {
      json += (i ? "," : "") + std::to_string(start + i / every);
    }
    json += "]}";
    firstKey = false;
  }
  return json + "}}";
}

// Adds a "landing_ms" list to a table JSON: velocity v lands at first - (v - 1) ms.
static std::string withLanding(const std::string &json, int first = 200, int count = 127)
{
  std::string list;
  for (int i = 0; i < count; i++)
  {
    list += (i ? "," : "") + std::to_string(first - i);
  }
  return json.substr(0, json.size() - 1) + ",\"landing_ms\":[" + list + "]}";
}

// Duties come back in hundredths of a percent.
static uint16_t hundredths(int percent)
{
  return (uint16_t)(percent * 100);
}

// The default mapping's duty at velocity 64, worked out from its range.
static const uint16_t DEFAULT_MID_DUTY = DEFAULT_MIN_DUTY * 100 + 63 * (DEFAULT_MAX_DUTY - DEFAULT_MIN_DUTY) * 100 / 126;

static bool load(VelocityTable &table, const std::string &json, std::string &error)
{
  return table.load(json.data(), json.size(), error);
}

void test_default_mapping_without_a_table()
{
  VelocityTable table;
  TEST_ASSERT_EQUAL_UINT16(hundredths(DEFAULT_MIN_DUTY), table.duty(40, 1));
  TEST_ASSERT_EQUAL_UINT16(DEFAULT_MID_DUTY, table.duty(40, 64));
  TEST_ASSERT_EQUAL_UINT16(hundredths(DEFAULT_MAX_DUTY), table.duty(40, 127));
  TEST_ASSERT_EQUAL_UINT16(hundredths(DEFAULT_MIN_DUTY), table.duty(40, 0));   // treated as velocity 1
  TEST_ASSERT_EQUAL_UINT16(hundredths(DEFAULT_MAX_DUTY), table.duty(40, 200)); // treated as 127
  TEST_ASSERT_EQUAL(0, table.calibratedKeys());
}

void test_loaded_keys_use_their_duties()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(load(table, tableJson({5, 40}), error), error.c_str());
  TEST_ASSERT_EQUAL(2, table.calibratedKeys());
  TEST_ASSERT_EQUAL_UINT16(hundredths(30), table.duty(40, 1));
  TEST_ASSERT_EQUAL_UINT16(hundredths(30 + 63 / 4), table.duty(40, 64));
  TEST_ASSERT_EQUAL_UINT16(hundredths(30 + 126 / 4), table.duty(5, 127));
  TEST_ASSERT_EQUAL_UINT16(hundredths(30), table.duty(5, 0));
}

void test_other_keys_keep_the_default()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE(load(table, tableJson({40}), error));
  TEST_ASSERT_FALSE(table.isCalibrated(87));
  TEST_ASSERT_EQUAL_UINT16(DEFAULT_MID_DUTY, table.duty(87, 64));
  TEST_ASSERT_EQUAL_UINT16(DEFAULT_MID_DUTY, table.duty(41, 64));
  TEST_ASSERT_EQUAL_UINT16(DEFAULT_MID_DUTY, table.duty(200, 64)); // not a key at all
}

static void assertRejected(const std::string &json, const char *expectedError)
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE(load(table, tableJson({40}, 50), error)); // a good table first
  TEST_ASSERT_FALSE(load(table, json, error));
  TEST_ASSERT_EQUAL_STRING(expectedError, error.c_str());
  // The good table is still in use.
  TEST_ASSERT_EQUAL(1, table.calibratedKeys());
  TEST_ASSERT_EQUAL_UINT16(hundredths(50), table.duty(40, 1));
}

void test_rejects_invalid_json()
{
  assertRejected("{\"keys\": {", "not valid JSON (IncompleteInput)");
}

void test_rejects_missing_keys()
{
  assertRejected("{\"generated\": \"x\"}", "no \"keys\" object");
}

void test_rejects_key_out_of_range()
{
  assertRejected(tableJson({88}), "key \"88\" is not 0-87");
}

void test_rejects_key_that_is_not_a_number()
{
  assertRejected("{\"keys\":{\"A0\":{\"duty\":[1]}}}", "key \"A0\" is not 0-87");
}

void test_rejects_wrong_number_of_duties()
{
  assertRejected(tableJson({40}, 30, 4, 126), "key 40: \"duty\" must list 127 values");
}

void test_rejects_duty_over_100()
{
  assertRejected(tableJson({40}, 90, 4), "key 40: duties must be numbers 0-100");
}

void test_rejects_duty_that_is_not_a_number()
{
  std::string json = tableJson({40});
  json.replace(json.find("[30,"), 4, "[\"30\",");
  assertRejected(json, "key 40: duties must be numbers 0-100");
}

void test_fractional_duties_keep_their_hundredths()
{
  std::string json = tableJson({40});
  json.replace(json.find("[30,30,30,30,"), 13, "[30.25,30.5,30.75,30.1,");
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(load(table, json, error), error.c_str());
  TEST_ASSERT_EQUAL_UINT16(3025, table.duty(40, 1));
  TEST_ASSERT_EQUAL_UINT16(3050, table.duty(40, 2));
  TEST_ASSERT_EQUAL_UINT16(3075, table.duty(40, 3));
  TEST_ASSERT_EQUAL_UINT16(3010, table.duty(40, 4));
}

// The table tools/calibrate_dynamics.py wrote for this piano.
void test_no_landing_times_without_the_list()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE(load(table, tableJson({40}), error));
  TEST_ASSERT_FALSE(table.hasLanding());
  TEST_ASSERT_EQUAL_UINT16(0, table.landingMs(40, 64));
  TEST_ASSERT_EQUAL_UINT16(0, table.maxLandingMs());
}

void test_landing_times_load()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(load(table, withLanding(tableJson({40})), error), error.c_str());
  TEST_ASSERT_TRUE(table.hasLanding());
  TEST_ASSERT_EQUAL_UINT16(200, table.landingMs(40, 1));
  TEST_ASSERT_EQUAL_UINT16(137, table.landingMs(40, 64));
  TEST_ASSERT_EQUAL_UINT16(74, table.landingMs(40, 127));
  TEST_ASSERT_EQUAL_UINT16(200, table.landingMs(40, 0));   // treated as velocity 1
  TEST_ASSERT_EQUAL_UINT16(74, table.landingMs(40, 200));  // treated as 127
  TEST_ASSERT_EQUAL_UINT16(200, table.maxLandingMs());
}

void test_rejects_wrong_number_of_landing_times()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_FALSE(load(table, withLanding(tableJson({40}), 200, 126), error));
  TEST_ASSERT_EQUAL_STRING("\"landing_ms\" must list 127 values", error.c_str());
}

void test_rejects_landing_time_out_of_range()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_FALSE(load(table, withLanding(tableJson({40}), MAX_LANDING_MS + 1), error));
  TEST_ASSERT_FALSE(load(table, withLanding(tableJson({40}), 100), error)); // ends at -26
}

void test_bad_landing_times_leave_the_table_unchanged()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE(load(table, withLanding(tableJson({40}), 150), error));
  TEST_ASSERT_FALSE(load(table, withLanding(tableJson({40, 41}), 150, 3), error));
  TEST_ASSERT_TRUE(table.hasLanding());
  TEST_ASSERT_EQUAL_UINT16(150, table.landingMs(40, 1));
  TEST_ASSERT_FALSE(table.isCalibrated(41));
}

// Key 40's entry gets its own "landing_ms": velocity v lands at own - (v - 1) ms.
static std::string withOwnLanding(const std::string &json, int own, int count = 127)
{
  std::string list;
  for (int i = 0; i < count; i++)
  {
    list += (i ? "," : "") + std::to_string(own - i);
  }
  std::string entry = "\"40\":{";
  size_t at = json.find(entry);
  return json.substr(0, at + entry.size()) + "\"landing_ms\":[" + list + "]," + json.substr(at + entry.size());
}

void test_a_key_uses_its_own_landing_times()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(load(table, withLanding(withOwnLanding(tableJson({5, 40}), 300), 200), error),
                           error.c_str());
  TEST_ASSERT_EQUAL_UINT16(300, table.landingMs(40, 1));
  TEST_ASSERT_EQUAL_UINT16(174, table.landingMs(40, 127));
  TEST_ASSERT_EQUAL_UINT16(200, table.landingMs(5, 1));  // the shared curve
  TEST_ASSERT_EQUAL_UINT16(200, table.landingMs(87, 1)); // uncalibrated keys too
  TEST_ASSERT_EQUAL_UINT16(300, table.maxLandingMs());
}

void test_own_landing_times_without_a_shared_curve()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE(load(table, withOwnLanding(tableJson({5, 40}), 150), error));
  TEST_ASSERT_TRUE(table.hasLanding());
  TEST_ASSERT_EQUAL_UINT16(150, table.landingMs(40, 1));
  TEST_ASSERT_EQUAL_UINT16(0, table.landingMs(5, 1)); // unknown
}

void test_rejects_bad_landing_times_for_a_key()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_FALSE(load(table, withOwnLanding(tableJson({40}), 150, 10), error));
  TEST_ASSERT_EQUAL_STRING("key 40: \"landing_ms\" must list 127 values", error.c_str());
}

// Puts extra fields into key 40's entry, e.g. "\"hold\":35".
static std::string withKey40(const std::string &json, const std::string &fields)
{
  std::string entry = "\"40\":{";
  size_t at = json.find(entry);
  return json.substr(0, at + entry.size()) + fields + "," + json.substr(at + entry.size());
}

void test_keys_use_the_drive_defaults_without_their_own()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE(load(table, tableJson({40}), error));
  TEST_ASSERT_EQUAL_UINT16(2000, table.coastDuty(40));
  TEST_ASSERT_EQUAL_INT32(10, table.setting(40, KeySetting::CoastOffsetMs, 5000));
  TEST_ASSERT_EQUAL_INT32(0, table.setting(40, KeySetting::ClampMs, 5000)); // no clamp
  TEST_ASSERT_EQUAL_UINT16(2500, table.holdDuty(40, 5000));
  TEST_ASSERT_EQUAL_UINT16(2500, table.holdDuty(200, 5000)); // not a key
}

void test_a_key_can_have_its_own_coast_and_hold()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(load(table, withKey40(tableJson({5, 40}), "\"coast\":22.5,\"hold\":35"), error),
                           error.c_str());
  TEST_ASSERT_EQUAL_UINT16(2250, table.coastDuty(40));
  TEST_ASSERT_EQUAL_UINT16(3500, table.holdDuty(40, 5000));
  TEST_ASSERT_EQUAL_UINT16(2500, table.holdDuty(5, 5000));
}

void test_hold_steps_follow_the_strike_duty()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(load(table, withKey40(tableJson({40}), "\"hold\":[[0,35],[45,40]]"), error), error.c_str());
  TEST_ASSERT_EQUAL_UINT16(3500, table.holdDuty(40, 3000));
  TEST_ASSERT_EQUAL_UINT16(3500, table.holdDuty(40, 4500)); // "above" 45: 45 itself is the lower step
  TEST_ASSERT_EQUAL_UINT16(4000, table.holdDuty(40, 4525));
  TEST_ASSERT_EQUAL_UINT16(4000, table.holdDuty(40, 10000));
}

void test_rejects_hold_steps_out_of_order()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_FALSE(load(table, withKey40(tableJson({40}), "\"hold\":[[45,40],[0,35]]"), error));
  TEST_ASSERT_FALSE(load(table, withKey40(tableJson({40}), "\"hold\":[[0,35],[10,36],[20,37],[30,38],[40,39],[50,40],[60,41]]"), error));
  TEST_ASSERT_FALSE(load(table, withKey40(tableJson({40}), "\"hold\":[[0,35],[45]]"), error));
}

void test_rejects_a_hold_out_of_range()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_FALSE(load(table, withKey40(tableJson({40}), "\"hold\":120"), error));
  TEST_ASSERT_EQUAL_STRING("key 40: hold must be a duty from 0 to 100", error.c_str());
  TEST_ASSERT_FALSE(load(table, withKey40(tableJson({40}), "\"coast\":0"), error));
}

// A table where key 40 plays 40% at every velocity and its hammer lands at
// `landing` ms, with extra fields for key 40 and the table.
static std::string pressTable(int landing, const std::string &keyFields = "", const std::string &tableFields = "")
{
  std::string duties, lands;
  for (int i = 0; i < 127; i++)
  {
    duties += (i ? "," : "") + std::string("40");
    lands += (i ? "," : "") + std::to_string(landing);
  }
  return "{\"keys\":{\"40\":{" + keyFields + (keyFields.empty() ? "" : ",") + "\"duty\":[" + duties +
         "],\"landing_ms\":[" + lands + "]}}" + (tableFields.empty() ? "" : ",") + tableFields + "}";
}

static std::string press(const std::string &json, uint8_t key = 40)
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(table.load(json.data(), json.size(), error), error.c_str());
  char command[33];
  table.pressCommand(key, 64, command, sizeof(command));
  return command;
}

void test_press_without_landing_times_is_plain()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE(load(table, tableJson({40}, 33), error));
  char command[33];
  table.pressCommand(40, 1, command, sizeof(command));
  TEST_ASSERT_EQUAL_STRING("P:40:33", command);
}

void test_press_coasts_until_the_hold_is_due()
{
  // Lands at 100 ms; the hold is due 10 ms later, so 60 ms of coast after the 50 ms strike.
  TEST_ASSERT_EQUAL_STRING("P:40:40:50:20:60:25", press(pressTable(100)).c_str());
}

void test_press_can_start_the_hold_before_landing()
{
  // Without a clamp, a negative offset starts the hold early; never before the strike ends.
  TEST_ASSERT_EQUAL_STRING("P:40:40:50:20:30:25", press(pressTable(100, "\"coast_offset_ms\":-20")).c_str());
  TEST_ASSERT_EQUAL_STRING("P:40:40:50:20:0:25", press(pressTable(60, "\"coast_offset_ms\":-20")).c_str());
}

void test_press_clamps_after_coasting()
{
  // Lands at 100 ms; the clamp starts 15 ms before, so 35 ms of coast, then 60% for 40 ms, then the hold.
  std::string drive = "\"drive\":{\"clamp_offset_ms\":-15,\"clamp_ms\":40,\"clamp_duty\":60}";
  TEST_ASSERT_EQUAL_STRING("P:40:40:50:20:35:60:40:25", press(pressTable(100, "", drive)).c_str());
  // A clamp due within the strike starts right after it.
  TEST_ASSERT_EQUAL_STRING("P:40:40:50:20:0:60:40:25", press(pressTable(60, "", drive)).c_str());
}

void test_press_uses_the_keys_own_drive()
{
  std::string own = "\"coast\":22.5,\"clamp_offset_ms\":-10,\"clamp_ms\":30,\"clamp_duty\":65,"
                    "\"hold\":[[0,35],[35,40]]";
  TEST_ASSERT_EQUAL_STRING("P:40:40:50:22.5:40:65:30:40", press(pressTable(100, own)).c_str());
  std::string later = "\"coast\":22.5,\"coast_offset_ms\":10";
  TEST_ASSERT_EQUAL_STRING("P:40:40:50:22.5:60:25", press(pressTable(100, later)).c_str());
}

void test_clamp_steps_follow_the_strike_duty()
{
  // A clamp only on strikes above 39.5%: key 40 plays at 40%.
  std::string above = "\"clamp_ms\":[[0,0],[39.5,40]]";
  TEST_ASSERT_EQUAL_STRING("P:40:40:50:20:35:60:40:25",
                           press(pressTable(100, above, "\"drive\":{\"clamp_offset_ms\":-15}")).c_str());
  std::string higher = "\"clamp_ms\":[[0,0],[45,40]]";
  TEST_ASSERT_EQUAL_STRING("P:40:40:50:20:60:25", press(pressTable(100, higher)).c_str()); // 40% isn't above 45
  VelocityTable table;
  std::string error;
  TEST_ASSERT_FALSE(load(table, pressTable(100, "\"clamp_ms\":[[45,40],[0,0]]"), error));
  TEST_ASSERT_FALSE(load(table, pressTable(100, "\"clamp_offset_ms\":[[0,-10.5]]"), error)); // whole ms only
}

void test_drive_replaces_the_defaults()
{
  VelocityTable table;
  std::string error;
  std::string json = pressTable(100, "", "\"drive\":{\"strike_ms\":45,\"coast_duty\":21,\"coast_offset_ms\":-5,"
                                         "\"clamp_offset_ms\":-20,\"clamp_duty\":55.5,\"clamp_ms\":35,\"hold_duty\":30}");
  TEST_ASSERT_TRUE_MESSAGE(load(table, json, error), error.c_str());
  TEST_ASSERT_EQUAL_UINT16(45, table.drive().strikeMs);
  TEST_ASSERT_EQUAL_INT16(-5, table.drive().coastOffsetMs);
  TEST_ASSERT_EQUAL_INT16(-20, table.drive().clampOffsetMs);
  TEST_ASSERT_EQUAL_UINT16(5550, table.drive().clampDuty);
  TEST_ASSERT_EQUAL_UINT16(3000, table.holdDuty(40, 4000));
  TEST_ASSERT_EQUAL_STRING("P:40:40:45:21:35:55.5:35:30", press(json).c_str());
}

void test_rejects_bad_drive_settings()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_FALSE(load(table, pressTable(100, "", "\"drive\":{\"coast_offset_ms\":-900}"), error));
  TEST_ASSERT_EQUAL_STRING("drive coast_offset_ms must be a whole number of ms from -500 to 500", error.c_str());
  TEST_ASSERT_FALSE(load(table, pressTable(100, "", "\"drive\":{\"clamp_duty\":120}"), error));
  TEST_ASSERT_FALSE(load(table, pressTable(100, "\"coast_offset_ms\":2.5"), error));
}

void test_generated_table_loads()
{
  FILE *f = fopen(GENERATED_TABLE, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "run `calibrate_dynamics.py table` first");
  std::string json;
  char buffer[4096];
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
  {
    json.append(buffer, n);
  }
  fclose(f);

  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(load(table, json, error), error.c_str());
  TEST_ASSERT_TRUE(table.calibratedKeys() > 0);
  for (uint8_t key = 0; key < PIANO_KEY_COUNT; key++)
  {
    for (uint8_t velocity = 2; velocity <= VELOCITY_LEVELS; velocity++)
    {
      char message[48];
      snprintf(message, sizeof(message), "key %u velocity %u", key, velocity);
      TEST_ASSERT_TRUE_MESSAGE(table.duty(key, velocity) >= table.duty(key, velocity - 1), message);
      TEST_ASSERT_TRUE_MESSAGE(table.duty(key, velocity) <= DUTY_FULL, message);
    }
  }
}

// A pedal entry for key 40: duty 35, landing 80, its own hold.
static const std::string PEDAL_ENTRY = []()
{
  std::string duties, lands;
  for (int i = 0; i < 127; i++)
  {
    duties += (i ? "," : "") + std::string("35");
    lands += (i ? "," : "") + std::string("80");
  }
  return "\"pedal\":{\"duty\":[" + duties + "],\"landing_ms\":[" + lands + "],\"hold\":30}";
}();

void test_pedal_entry_plays_with_the_pedal_down()
{
  // With the pedal up: the key's own clamp. Down: the pedal entry's duty, landing
  // and hold, and no clamp, since the entry doesn't have one.
  std::string json = pressTable(100, "\"clamp_offset_ms\":-10,\"clamp_ms\":30,\"clamp_duty\":65," + PEDAL_ENTRY);
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(load(table, json, error), error.c_str());
  TEST_ASSERT_TRUE(table.hasPedal(40));
  TEST_ASSERT_FALSE(table.hasPedal(41));
  TEST_ASSERT_EQUAL(1, table.pedalKeys());
  TEST_ASSERT_EQUAL_UINT16(4000, table.duty(40, 64));
  TEST_ASSERT_EQUAL_UINT16(3500, table.duty(40, 64, true));
  TEST_ASSERT_EQUAL_UINT16(100, table.landingMs(40, 64));
  TEST_ASSERT_EQUAL_UINT16(80, table.landingMs(40, 64, true));
  char command[49];
  table.pressCommand(40, 64, command, sizeof(command));
  TEST_ASSERT_EQUAL_STRING("P:40:40:50:20:40:65:30:25", command);
  table.pressCommand(40, 64, command, sizeof(command), true);
  TEST_ASSERT_EQUAL_STRING("P:40:35:50:20:40:30", command);
}

void test_key_without_a_pedal_entry_plays_the_same_either_way()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE(load(table, withLanding(tableJson({40, 41})), error));
  TEST_ASSERT_EQUAL(0, table.pedalKeys());
  TEST_ASSERT_EQUAL_UINT16(table.duty(40, 64), table.duty(40, 64, true));
  TEST_ASSERT_EQUAL_UINT16(table.landingMs(40, 64), table.landingMs(40, 64, true));
  char up[49], down[49];
  table.pressCommand(40, 64, up, sizeof(up));
  table.pressCommand(40, 64, down, sizeof(down), true);
  TEST_ASSERT_EQUAL_STRING(up, down);
}

void test_rejects_a_pedal_entry_without_landing_times()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE(load(table, pressTable(100, PEDAL_ENTRY), error));
  std::string duties;
  for (int i = 0; i < 127; i++)
  {
    duties += (i ? "," : "") + std::string("30");
  }
  TEST_ASSERT_FALSE(load(table, pressTable(100, "\"pedal\":{\"duty\":[" + duties + "]}"), error));
  TEST_ASSERT_EQUAL_STRING("key 40 pedal: needs \"landing_ms\"", error.c_str());
  TEST_ASSERT_TRUE(table.hasPedal(40)); // unchanged
  TEST_ASSERT_EQUAL_UINT16(3500, table.duty(40, 1, true));
}

void test_latest_landing_includes_the_pedal_entries()
{
  VelocityTable table;
  std::string error;
  TEST_ASSERT_TRUE(load(table, pressTable(60, PEDAL_ENTRY), error));
  TEST_ASSERT_EQUAL_UINT16(80, table.maxLandingMs());
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_default_mapping_without_a_table);
  RUN_TEST(test_loaded_keys_use_their_duties);
  RUN_TEST(test_other_keys_keep_the_default);
  RUN_TEST(test_rejects_invalid_json);
  RUN_TEST(test_rejects_missing_keys);
  RUN_TEST(test_rejects_key_out_of_range);
  RUN_TEST(test_rejects_key_that_is_not_a_number);
  RUN_TEST(test_rejects_wrong_number_of_duties);
  RUN_TEST(test_rejects_duty_over_100);
  RUN_TEST(test_rejects_duty_that_is_not_a_number);
  RUN_TEST(test_fractional_duties_keep_their_hundredths);
  RUN_TEST(test_no_landing_times_without_the_list);
  RUN_TEST(test_landing_times_load);
  RUN_TEST(test_rejects_wrong_number_of_landing_times);
  RUN_TEST(test_rejects_landing_time_out_of_range);
  RUN_TEST(test_bad_landing_times_leave_the_table_unchanged);
  RUN_TEST(test_a_key_uses_its_own_landing_times);
  RUN_TEST(test_own_landing_times_without_a_shared_curve);
  RUN_TEST(test_rejects_bad_landing_times_for_a_key);
  RUN_TEST(test_keys_use_the_drive_defaults_without_their_own);
  RUN_TEST(test_a_key_can_have_its_own_coast_and_hold);
  RUN_TEST(test_hold_steps_follow_the_strike_duty);
  RUN_TEST(test_rejects_hold_steps_out_of_order);
  RUN_TEST(test_rejects_a_hold_out_of_range);
  RUN_TEST(test_press_without_landing_times_is_plain);
  RUN_TEST(test_press_coasts_until_the_hold_is_due);
  RUN_TEST(test_press_can_start_the_hold_before_landing);
  RUN_TEST(test_press_clamps_after_coasting);
  RUN_TEST(test_press_uses_the_keys_own_drive);
  RUN_TEST(test_clamp_steps_follow_the_strike_duty);
  RUN_TEST(test_drive_replaces_the_defaults);
  RUN_TEST(test_rejects_bad_drive_settings);
  RUN_TEST(test_pedal_entry_plays_with_the_pedal_down);
  RUN_TEST(test_key_without_a_pedal_entry_plays_the_same_either_way);
  RUN_TEST(test_rejects_a_pedal_entry_without_landing_times);
  RUN_TEST(test_latest_landing_includes_the_pedal_entries);
  RUN_TEST(test_generated_table_loads);
  return UNITY_END();
}
