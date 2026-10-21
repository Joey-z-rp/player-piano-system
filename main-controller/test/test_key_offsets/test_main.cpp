#include <unity.h>
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
  return UNITY_END();
}
