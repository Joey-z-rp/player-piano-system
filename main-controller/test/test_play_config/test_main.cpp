#include <unity.h>
#include <string>
#include "app/piano/play_config.h"

void setUp()
{
}

void tearDown()
{
}

static bool read(PlayConfig &config, const std::string &json, std::string &error)
{
  return readPlayConfig(json.data(), json.size(), config, error);
}

void test_defaults_survive_a_round_trip()
{
  PlayConfig config;
  std::string json = playConfigJson(config);
  PlayConfig back;
  back.minNoteMs = 99; // overwritten by the JSON
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(read(back, json, error), error.c_str());
  TEST_ASSERT_TRUE(back == config);
}

void test_settings_not_given_keep_their_values()
{
  PlayConfig config;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(read(config, "{\"min_note_ms\":15,\"repeat_clamp_duty\":80.5}", error), error.c_str());
  TEST_ASSERT_EQUAL_UINT16(15, config.minNoteMs);
  TEST_ASSERT_EQUAL_UINT16(8050, config.repeatClamp.duty);
  TEST_ASSERT_EQUAL_UINT16(70, config.restrikeGapMs);
  TEST_ASSERT_EQUAL_UINT8(5, config.repeatLimitCount);
}

void test_reads_repeat_limits_and_dead_zone()
{
  PlayConfig config;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(
      read(config, "{\"repeat_limits\":[[90,100,130],[0,180,200]],\"repeat_dead_zone\":[70,95,100,115]}", error),
      error.c_str());
  TEST_ASSERT_EQUAL_UINT8(2, config.repeatLimitCount);
  TEST_ASSERT_EQUAL_UINT8(90, config.repeatLimits[0].from);
  TEST_ASSERT_EQUAL_UINT16(200, config.repeatLimits[1].afterLoudMs);
  TEST_ASSERT_EQUAL_UINT8(70, config.deadZoneFromVelocity);
  TEST_ASSERT_EQUAL_UINT16(115, config.deadZoneToMs);
}

void test_bad_settings_change_nothing()
{
  PlayConfig config;
  std::string error;
  const char *bad[] = {
      "{\"min_note_ms\":15,\"restrike_gap_ms\":-1}",
      "{\"repeat_limits\":[[0,100,100],[50,100,100]]}", // not in falling order
      "{\"repeat_limits\":[[50,100,100]]}",             // the last isn't from 0
      "{\"repeat_dead_zone\":[1,2,3]}",
      "{\"pedal_press\":\"100;30\"}",
      "[1,2]",
      "not json",
  };
  for (const char *json : bad)
  {
    error.clear();
    TEST_ASSERT_FALSE_MESSAGE(read(config, json, error), json);
    TEST_ASSERT_FALSE(error.empty());
  }
  TEST_ASSERT_TRUE(config == PlayConfig());
}

void test_pedal_commands()
{
  PlayConfig config;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(read(config, "{\"pedal_press\":\"90:40:55\",\"pedal_release\":\"0\"}", error),
                           error.c_str());
  TEST_ASSERT_EQUAL_STRING("90:40:55", config.pedalPress);
  TEST_ASSERT_EQUAL_STRING("0", config.pedalRelease);
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_defaults_survive_a_round_trip);
  RUN_TEST(test_settings_not_given_keep_their_values);
  RUN_TEST(test_reads_repeat_limits_and_dead_zone);
  RUN_TEST(test_bad_settings_change_nothing);
  RUN_TEST(test_pedal_commands);
  return UNITY_END();
}
