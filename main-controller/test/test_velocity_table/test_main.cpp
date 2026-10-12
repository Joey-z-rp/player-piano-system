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
  RUN_TEST(test_generated_table_loads);
  return UNITY_END();
}
