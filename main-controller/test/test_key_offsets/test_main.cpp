#include <unity.h>
#include <memory>
#include <string>
#include "app/piano/key_offsets.h"
#include "app/piano/velocity_table.h"

void setUp()
{
}

void tearDown()
{
}

static OffsetCurve curve(const std::string &json)
{
  OffsetCurve out;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(readOffsetCurve(json.data(), json.size(), out, error), error.c_str());
  return out;
}

static std::string rejected(const std::string &json)
{
  OffsetCurve out = curve("[[64,-4]]");
  std::string error;
  TEST_ASSERT_FALSE(readOffsetCurve(json.data(), json.size(), out, error));
  TEST_ASSERT_EQUAL_UINT8(1, out.count); // unchanged
  return error;
}

void test_no_points_is_no_offset()
{
  OffsetCurve none = curve("[]");
  TEST_ASSERT_EQUAL_UINT8(1, none.adjust(1));
  TEST_ASSERT_EQUAL_UINT8(64, none.adjust(64));
  TEST_ASSERT_EQUAL_UINT8(127, none.adjust(127));
}

void test_one_point_offsets_every_velocity()
{
  OffsetCurve down = curve("[[64,-4]]");
  TEST_ASSERT_EQUAL_UINT8(6, down.adjust(10));
  TEST_ASSERT_EQUAL_UINT8(60, down.adjust(64));
  TEST_ASSERT_EQUAL_UINT8(123, down.adjust(127));
}

void test_points_are_joined_by_straight_lines_and_flat_beyond()
{
  // 0 up to 40, down to -4 at 64, back to 0 at 90 and above.
  OffsetCurve middle = curve("[[40,0],[64,-4],[90,0]]");
  TEST_ASSERT_EQUAL_UINT8(20, middle.adjust(20));
  TEST_ASSERT_EQUAL_UINT8(40, middle.adjust(40));
  TEST_ASSERT_EQUAL_UINT8(50, middle.adjust(52)); // -2 halfway
  TEST_ASSERT_EQUAL_UINT8(60, middle.adjust(64));
  TEST_ASSERT_EQUAL_UINT8(75, middle.adjust(77)); // -2 halfway back
  TEST_ASSERT_EQUAL_UINT8(100, middle.adjust(100));
}

void test_offsets_round_half_away_from_zero()
{
  OffsetCurve half = curve("[[10,0],[12,1]]");   // +0.5 at 11
  TEST_ASSERT_EQUAL_UINT8(12, half.adjust(11));
  OffsetCurve halfDown = curve("[[10,0],[12,-1]]"); // -0.5 at 11
  TEST_ASSERT_EQUAL_UINT8(10, halfDown.adjust(11));
}

void test_velocities_stay_within_1_and_127()
{
  TEST_ASSERT_EQUAL_UINT8(1, curve("[[64,-20]]").adjust(5));
  TEST_ASSERT_EQUAL_UINT8(127, curve("[[64,10]]").adjust(120));
}

void test_bad_curves_are_rejected_and_change_nothing()
{
  rejected("[[64,-4],[40,0]]");               // not rising
  rejected("[[0,-4]]");                       // velocity 0
  rejected("[[64,61]]");                      // too big
  curve("[[1,0],[2,0],[3,0],[4,0],[5,0],[6,0],[7,0],[8,0]]");            // 8 is the most
  rejected("[[1,0],[2,0],[3,0],[4,0],[5,0],[6,0],[7,0],[8,0],[9,0]]"); // too many
  // Falling faster than 1 per velocity: 70 would play softer than 69.
  std::string error = rejected("[[60,0],[70,-20]]");
  TEST_ASSERT_TRUE(error.find("softer") != std::string::npos);
  // Exactly 1 per velocity is allowed: those velocities play alike.
  curve("[[60,0],[70,-10]]");
}

void test_pedal_down_uses_its_own_curve_else_the_pedal_up_one()
{
  std::string json = "{\"keys\":{\"10\":{\"up\":[[64,-4]]},\"20\":{\"up\":[[64,-4]],\"pedal\":[[64,6]]}}}";
  KeyOffsets offsets;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(readKeyOffsets(json.data(), json.size(), offsets, error), error.c_str());
  TEST_ASSERT_EQUAL_UINT8(60, offsets.adjust(10, 64, false));
  TEST_ASSERT_EQUAL_UINT8(60, offsets.adjust(10, 64, true));
  TEST_ASSERT_EQUAL_UINT8(60, offsets.adjust(20, 64, false));
  TEST_ASSERT_EQUAL_UINT8(70, offsets.adjust(20, 64, true));
  TEST_ASSERT_EQUAL_UINT8(64, offsets.adjust(30, 64, true));
  TEST_ASSERT_EQUAL_STRING(json.c_str(), keyOffsetsJson(offsets).c_str());
}

void test_bad_key_offsets_change_nothing()
{
  KeyOffsets offsets;
  offsets.up[5].points[0] = {64, -4};
  offsets.up[5].count = 1;
  std::string error;
  std::string json = "{\"keys\":{\"88\":{\"up\":[[64,-4]]}}}";
  TEST_ASSERT_FALSE(readKeyOffsets(json.data(), json.size(), offsets, error));
  json = "{\"keys\":{\"3\":{\"up\":[[64,-4]],\"pedal\":[[64,-4],[70,-20]]}}}";
  TEST_ASSERT_FALSE(readKeyOffsets(json.data(), json.size(), offsets, error));
  TEST_ASSERT_TRUE(error.find("key 3") == 0);
  TEST_ASSERT_EQUAL_UINT8(1, offsets.up[5].count);
}

// A table: key 0's duty is the velocity (in percent, so 64 -> 64%), and its
// hammer lands 200 - velocity ms after the press.
static void loadTable(VelocityTable &table)
{
  std::string duty = "[", landing = "[";
  for (int v = 1; v <= 127; v++)
  {
    duty += (v > 1 ? "," : "") + std::to_string(v * 100 / 127.0);
    landing += (v > 1 ? "," : "") + std::to_string(200 - v);
  }
  std::string json = "{\"keys\":{\"0\":{\"duty\":" + duty + "],\"landing_ms\":" + landing + "]}}}";
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(table.load(json.data(), json.size(), error), error.c_str());
}

void test_the_table_plays_a_key_at_its_offset_velocity()
{
  static VelocityTable table;
  loadTable(table);
  uint16_t at60 = table.duty(0, 60);
  uint16_t landing60 = table.landingMs(0, 60);
  char plain60[48];
  table.pressCommand(0, 60, plain60, sizeof(plain60));

  std::shared_ptr<KeyOffsets> offsets = std::make_shared<KeyOffsets>();
  offsets->up[0].points[0] = {64, -4};
  offsets->up[0].count = 1;
  table.setOffsets(offsets);
  TEST_ASSERT_EQUAL_UINT8(60, table.playedVelocity(0, 64));
  TEST_ASSERT_EQUAL_UINT16(at60, table.duty(0, 64));
  TEST_ASSERT_EQUAL_UINT16(landing60, table.landingMs(0, 64));
  TEST_ASSERT_NOT_EQUAL(at60, table.tableDuty(0, 64)); // the table as built
  char adjusted[48];
  table.pressCommand(0, 64, adjusted, sizeof(adjusted));
  TEST_ASSERT_EQUAL_STRING(plain60, adjusted);
  TEST_ASSERT_EQUAL_UINT16(199, table.maxLandingMs()); // the lead doesn't move
  table.setOffsets(nullptr);
  TEST_ASSERT_EQUAL_UINT8(64, table.playedVelocity(0, 64));
}

static KeyOffsets::Entry entry(const std::string &json)
{
  KeyOffsets::Entry out;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(readKeyEntry(json.data(), json.size(), out, error), error.c_str());
  return out;
}

void test_duty_changes_join_points_in_quarter_percent_steps()
{
  KeyOffsets::Entry e = entry("{\"duty\":[[64,0],[127,23]]}");
  TEST_ASSERT_EQUAL_INT16(0, e.dutyUp.change(1)); // flat before the first point
  TEST_ASSERT_EQUAL_INT16(0, e.dutyUp.change(64));
  TEST_ASSERT_EQUAL_INT16(1325, e.dutyUp.change(100)); // 13.14% -> 13.25%
  TEST_ASSERT_EQUAL_INT16(2300, e.dutyUp.change(127));
  TEST_ASSERT_EQUAL_INT16(-250, entry("{\"duty\":[[1,-2.5]]}").dutyUp.change(90)); // one point: everywhere
  TEST_ASSERT_EQUAL_INT16(0, entry("{}").dutyUp.change(90));
  // A curve may fall: whether the duties still rise depends on the table.
  TEST_ASSERT_EQUAL_INT16(-400, entry("{\"duty\":[[70,0],[90,-4],[110,0]]}").dutyUp.change(90));
}

void test_bad_duty_curves_are_rejected_and_change_nothing()
{
  KeyOffsets::Entry e = entry("{\"duty\":[[64,5]]}");
  std::string error;
  for (const char *json : {"{\"duty\":[[64,0.3]]}", "{\"duty\":[[64,51]]}", "{\"duty\":[[90,1],[80,2]]}",
                           "{\"duty\":[[0,1]]}", "{\"duty\":[[64,\"5\"]]}", "{\"duty_pedal\":[64,5]}"})
  {
    std::string body = json;
    TEST_ASSERT_FALSE_MESSAGE(readKeyEntry(body.data(), body.size(), e, error), json);
  }
  TEST_ASSERT_EQUAL_INT16(500, e.dutyUp.change(64));
}

void test_duty_curves_are_kept_with_the_offsets()
{
  std::string json = "{\"keys\":{\"10\":{\"up\":[],\"duty\":[[64,0],[127,23.25]]},"
                     "\"20\":{\"up\":[[64,-4]],\"duty_pedal\":[[1,-1.5]]}}}";
  KeyOffsets offsets;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(readKeyOffsets(json.data(), json.size(), offsets, error), error.c_str());
  TEST_ASSERT_EQUAL_INT16(2325, offsets.dutyChange(10, 127, false));
  TEST_ASSERT_EQUAL_INT16(2325, offsets.dutyChange(10, 127, true)); // no pedal-down curve: the pedal-up one
  TEST_ASSERT_EQUAL_INT16(0, offsets.dutyChange(20, 90, false));
  TEST_ASSERT_EQUAL_INT16(-150, offsets.dutyChange(20, 90, true));
  TEST_ASSERT_FALSE(offsets.empty());
  TEST_ASSERT_EQUAL_STRING(json.c_str(), keyOffsetsJson(offsets).c_str());
  offsets.set(10, KeyOffsets::Entry());
  offsets.set(20, KeyOffsets::Entry());
  TEST_ASSERT_TRUE(offsets.empty());
}

// Keys 10-14: each one's duty rises to 100% at velocity 127 and its hammer
// lands 200 - velocity ms after the press, except key 12's, which only goes
// to 50% (its calibration stopped short).
static void loadNeighbours(VelocityTable &table)
{
  std::string keys;
  for (int key = 10; key <= 14; key++)
  {
    std::string duty = "[", landing = "[";
    for (int v = 1; v <= 127; v++)
    {
      duty += (v > 1 ? "," : "") + std::to_string(v * (key == 12 ? 50 : 100) / 127.0);
      landing += (v > 1 ? "," : "") + std::to_string(200 - v);
    }
    keys += (key > 10 ? ",\"" : "\"") + std::to_string(key) + "\":{\"duty\":" + duty + "],\"landing_ms\":" + landing + "]}";
  }
  std::string json = "{\"keys\":{" + keys + "}}";
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(table.load(json.data(), json.size(), error), error.c_str());
}

static void useDuty(VelocityTable &table, uint8_t key, const std::string &json)
{
  std::shared_ptr<KeyOffsets> offsets = std::make_shared<KeyOffsets>();
  offsets->set(key, entry(json));
  table.setOffsets(offsets);
}

void test_a_duty_change_adds_to_the_table_within_0_and_100()
{
  static VelocityTable table;
  loadNeighbours(table);
  useDuty(table, 12, "{\"duty\":[[64,0],[127,30]]}");
  TEST_ASSERT_EQUAL_UINT16(table.tableDuty(12, 64), table.duty(12, 64));
  TEST_ASSERT_EQUAL_UINT16(8000, table.duty(12, 127));
  char command[48];
  table.pressCommand(12, 127, command, sizeof(command));
  TEST_ASSERT_EQUAL_STRING_LEN("P:12:80:", command, 8);
  useDuty(table, 11, "{\"duty\":[[1,10]]}");
  TEST_ASSERT_EQUAL_UINT16(DUTY_FULL, table.duty(11, 127));
  useDuty(table, 11, "{\"duty\":[[1,-10]]}");
  TEST_ASSERT_EQUAL_UINT16(0, table.duty(11, 1));
  table.setOffsets(nullptr);
}

void test_a_duty_change_lands_as_its_duty_does()
{
  static VelocityTable table;
  loadNeighbours(table);
  // Within the key's own duties: the landing of the velocity with that duty.
  // Velocity 64 is 25.2% + 5% = 30.2%, key 12's duty at about velocity 76.7.
  useDuty(table, 12, "{\"duty\":[[1,5]]}");
  TEST_ASSERT_UINT16_WITHIN(1, 123, table.landingMs(12, 64));
  // Below its softest: its softest's.
  useDuty(table, 12, "{\"duty\":[[1,-5]]}");
  TEST_ASSERT_EQUAL_UINT16(199, table.landingMs(12, 1));
  // Above its loudest (50%, landing at 73 ms): as much sooner as its
  // neighbours land from 50% to 80%, about 38 ms.
  useDuty(table, 12, "{\"duty\":[[64,0],[127,30]]}");
  TEST_ASSERT_UINT16_WITHIN(1, 35, table.landingMs(12, 127));
  TEST_ASSERT_EQUAL_UINT16(table.tableLandingMs(12, 64), table.landingMs(12, 64)); // no change: as before
  TEST_ASSERT_EQUAL_UINT16(199, table.maxLandingMs());
  // A key struck softer than its loudest: along its own times.
  useDuty(table, 10, "{\"duty\":[[127,-50]]}"); // 50%: velocity 63.5 -> about 136 ms
  TEST_ASSERT_UINT16_WITHIN(1, 136, table.landingMs(10, 127));
  table.setOffsets(nullptr);
}

void test_a_key_louder_than_its_neighbours_go_keeps_its_loudest_landing()
{
  static VelocityTable table;
  std::string duty = "[", landing = "[";
  for (int v = 1; v <= 127; v++)
  {
    duty += (v > 1 ? "," : "") + std::to_string(v * 50 / 127.0);
    landing += (v > 1 ? "," : "") + std::to_string(200 - v);
  }
  std::string json = "{\"keys\":{\"40\":{\"duty\":" + duty + "],\"landing_ms\":" + landing + "]}}}";
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(table.load(json.data(), json.size(), error), error.c_str());
  useDuty(table, 40, "{\"duty\":[[127,30]]}");
  TEST_ASSERT_EQUAL_UINT16(73, table.landingMs(40, 127));
  table.setOffsets(nullptr);
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_no_points_is_no_offset);
  RUN_TEST(test_one_point_offsets_every_velocity);
  RUN_TEST(test_points_are_joined_by_straight_lines_and_flat_beyond);
  RUN_TEST(test_offsets_round_half_away_from_zero);
  RUN_TEST(test_velocities_stay_within_1_and_127);
  RUN_TEST(test_bad_curves_are_rejected_and_change_nothing);
  RUN_TEST(test_pedal_down_uses_its_own_curve_else_the_pedal_up_one);
  RUN_TEST(test_bad_key_offsets_change_nothing);
  RUN_TEST(test_the_table_plays_a_key_at_its_offset_velocity);
  RUN_TEST(test_duty_changes_join_points_in_quarter_percent_steps);
  RUN_TEST(test_bad_duty_curves_are_rejected_and_change_nothing);
  RUN_TEST(test_duty_curves_are_kept_with_the_offsets);
  RUN_TEST(test_a_duty_change_adds_to_the_table_within_0_and_100);
  RUN_TEST(test_a_duty_change_lands_as_its_duty_does);
  RUN_TEST(test_a_key_louder_than_its_neighbours_go_keeps_its_loudest_landing);
  return UNITY_END();
}
