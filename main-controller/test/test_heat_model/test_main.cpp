#include <unity.h>
#include <math.h>
#include "app/piano/heat_model.h"

void setUp()
{
}

void tearDown()
{
}

// A coil of 24 ohms at 24 V: 24 W at 100%. 2 °C per watt, 100 s to settle.
static HeatModel model(bool squared = true)
{
  HeatSettings settings;
  settings.volts = 24;
  settings.squared = squared;
  settings.keys = {24, 2, 100};
  settings.pedal = {12, 1, 200};
  settings.limitC = 50;
  HeatModel heat;
  heat.configure(settings);
  return heat;
}

static const uint32_t LONG = 3600000; // an hour: settled

void test_unmeasured_coils_arent_watched()
{
  HeatModel heat;
  heat.command("P:40:50:50:50", 0);
  heat.update(LONG);
  TEST_ASSERT_FALSE(heat.watched(40));
  TEST_ASSERT_EQUAL_FLOAT(0, heat.riseC(40));
  TEST_ASSERT_EQUAL_UINT8(HEAT_CHANNELS, heat.hottest());
}

void test_a_hold_settles_at_power_times_rth()
{
  HeatModel heat = model();
  heat.command("P:40:50:50:50", 0); // 50% throughout: 24 W x 0.25 = 6 W -> 12 °C
  heat.update(LONG);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 12, heat.riseC(40));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.24, heat.load(40));
}

void test_heat_can_go_with_duty_instead()
{
  HeatModel heat = model(false);
  heat.command("P:40:50:50:50", 0); // 24 W x 0.5 = 12 W -> 24 °C
  heat.update(LONG);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 24, heat.riseC(40));
}

void test_it_warms_with_the_time_constant()
{
  HeatModel heat = model();
  heat.command("P:40:100:50:100", 0); // 24 W -> 48 °C, tau 100 s
  heat.update(100000);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 48 * (1 - expf(-1)), heat.riseC(40));
  // Updated in small steps, the same.
  HeatModel stepped = model();
  stepped.command("P:40:100:50:100", 0);
  for (uint32_t t = 250; t <= 100000; t += 250)
  {
    stepped.update(t);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.05, heat.riseC(40), stepped.riseC(40));
}

void test_release_cools_it()
{
  HeatModel heat = model();
  heat.command("P:40:100:50:100", 0);
  heat.update(LONG);
  heat.command("R:40:0", LONG);
  heat.update(LONG + 100000);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 48 * expf(-1), heat.riseC(40));
  TEST_ASSERT_FLOAT_WITHIN(1, 100, heat.secondsToCool(40, 0.0) + 100); // none: 0
}

void test_the_drivers_defaults_apply()
{
  // A plain press: the duty for 50 ms, then the drivers' 20% hold.
  HeatModel heat = model();
  heat.command("P:40:80", 0);
  heat.update(LONG);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 24 * 0.04f * 2, heat.riseC(40));
}

void test_strike_phases_and_hold_are_followed()
{
  // 100% for 1 s, then 50% for 1 s, then a hold of 0: the mean heating over
  // those 2 s is (1 + 0.25) / 2.
  HeatModel heat = model();
  heat.command("P:40:100:1000:50:1000:0", 0);
  HeatModel same = model();
  same.command("P:40:100:1000:100", 0);
  same.command("P:40:50:1000:50", 1000);
  same.command("R:40:0", 2000);
  heat.update(10000);
  same.update(10000);
  TEST_ASSERT_FLOAT_WITHIN(0.001, same.riseC(40), heat.riseC(40));
  TEST_ASSERT_TRUE(heat.riseC(40) > 0);
}

void test_the_pedal_has_its_own_coil()
{
  HeatModel heat = model();
  heat.command("P:88:100:30:100:30:55", 0); // 48 W at 100%: 55% hold -> 14.5 W -> 14.5 °C
  heat.update(LONG);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 48 * 0.55f * 0.55f, heat.riseC(88));
  TEST_ASSERT_EQUAL_UINT8(88, heat.hottest());
}

void test_reset_turns_everything_off()
{
  HeatModel heat = model();
  heat.command("P:40:100:50:100", 0);
  heat.command("P:88:100:50:100", 0);
  heat.update(LONG);
  heat.command("RESET:ALL", LONG);
  heat.update(LONG * 2);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0, heat.riseC(40));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0, heat.riseC(88));
}

void test_time_to_cool()
{
  HeatModel heat = model();
  heat.command("P:40:100:50:100", 0); // settles at 48 °C: load 0.96
  heat.update(LONG);
  heat.command("R:40:0", LONG);
  // To 0.48 load: tau x ln 2.
  TEST_ASSERT_FLOAT_WITHIN(0.5, 100 * logf(2), heat.secondsToCool(40, 0.48f));
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_unmeasured_coils_arent_watched);
  RUN_TEST(test_a_hold_settles_at_power_times_rth);
  RUN_TEST(test_heat_can_go_with_duty_instead);
  RUN_TEST(test_it_warms_with_the_time_constant);
  RUN_TEST(test_release_cools_it);
  RUN_TEST(test_the_drivers_defaults_apply);
  RUN_TEST(test_strike_phases_and_hold_are_followed);
  RUN_TEST(test_the_pedal_has_its_own_coil);
  RUN_TEST(test_reset_turns_everything_off);
  RUN_TEST(test_time_to_cool);
  return UNITY_END();
}
