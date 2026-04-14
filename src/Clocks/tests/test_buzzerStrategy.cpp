#include "unity.h"
#include "buzzerStrategy.h"

static BuzzerStrategy strategy;

static BuzzerStrategyInput makeInput(bool running, unsigned long nowMs, int coolant, int oil, int egt) {
  BuzzerStrategyInput input;
  input.engineRunning = running;
  input.nowMs = nowMs;
  input.coolantTemp = coolant;
  input.oilTemp = oil;
  input.egtTemp = egt;
  return input;
}

void setUp(void) {
  strategy.reset();
}

void tearDown(void) {}

void test_no_alarm_when_engine_not_running(void) {
  BuzzerStrategyInput input = makeInput(false, 0, BUZZER_COOLANT_OVERHEAT_TEMP + 5, BUZZER_OIL_OVERHEAT_TEMP + 5, BUZZER_EGT_OVERHEAT_TEMP + 5);
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_NONE, strategy.process(input));
}

void test_coolant_edge_triggers_middle(void) {
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_NONE, strategy.process(makeInput(true, 0, BUZZER_COOLANT_OVERHEAT_TEMP, 0, 0)));
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_MIDDLE, strategy.process(makeInput(true, 1, BUZZER_COOLANT_OVERHEAT_TEMP + 1, 0, 0)));
}

void test_oil_edge_triggers_middle(void) {
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_NONE, strategy.process(makeInput(true, 0, 0, BUZZER_OIL_OVERHEAT_TEMP, 0)));
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_MIDDLE, strategy.process(makeInput(true, 1, 0, BUZZER_OIL_OVERHEAT_TEMP + 1, 0)));
}

void test_coolant_oil_repeat_is_rate_limited_and_repeats_after_interval(void) {
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_MIDDLE, strategy.process(makeInput(true, 0, BUZZER_COOLANT_OVERHEAT_TEMP + 1, 0, 0)));
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_NONE, strategy.process(makeInput(true, BUZZER_COOLANT_OIL_REPEAT_INTERVAL_MS - 1, BUZZER_COOLANT_OVERHEAT_TEMP + 1, 0, 0)));
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_MIDDLE, strategy.process(makeInput(true, BUZZER_COOLANT_OIL_REPEAT_INTERVAL_MS, BUZZER_COOLANT_OVERHEAT_TEMP + 1, 0, 0)));
}

void test_egt_edge_triggers_long(void) {
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_NONE, strategy.process(makeInput(true, 0, 0, 0, BUZZER_EGT_OVERHEAT_TEMP)));
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_LONG, strategy.process(makeInput(true, 1, 0, 0, BUZZER_EGT_OVERHEAT_TEMP + 1)));
}

void test_egt_repeat_is_rate_limited_and_repeats_after_interval(void) {
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_LONG, strategy.process(makeInput(true, 0, 0, 0, BUZZER_EGT_OVERHEAT_TEMP + 1)));
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_NONE, strategy.process(makeInput(true, BUZZER_EGT_REPEAT_INTERVAL_MS - 1, 0, 0, BUZZER_EGT_OVERHEAT_TEMP + 1)));
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_LONG, strategy.process(makeInput(true, BUZZER_EGT_REPEAT_INTERVAL_MS, 0, 0, BUZZER_EGT_OVERHEAT_TEMP + 1)));
}

void test_alarm_resets_after_temperature_returns_to_safe_range(void) {
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_MIDDLE, strategy.process(makeInput(true, 0, BUZZER_COOLANT_OVERHEAT_TEMP + 1, 0, 0)));
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_NONE, strategy.process(makeInput(true, 1000, BUZZER_COOLANT_OVERHEAT_TEMP, 0, 0)));
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_MIDDLE, strategy.process(makeInput(true, 1001, BUZZER_COOLANT_OVERHEAT_TEMP + 1, 0, 0)));
}

void test_engine_stop_resets_alarm_state(void) {
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_LONG, strategy.process(makeInput(true, 0, 0, 0, BUZZER_EGT_OVERHEAT_TEMP + 1)));
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_NONE, strategy.process(makeInput(false, 1, 0, 0, BUZZER_EGT_OVERHEAT_TEMP + 1)));
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_LONG, strategy.process(makeInput(true, 2, 0, 0, BUZZER_EGT_OVERHEAT_TEMP + 1)));
}

void test_egt_has_priority_when_both_alarms_fire_together(void) {
  TEST_ASSERT_EQUAL_INT(BUZZER_STRATEGY_LONG, strategy.process(makeInput(true, 0, BUZZER_COOLANT_OVERHEAT_TEMP + 1, 0, BUZZER_EGT_OVERHEAT_TEMP + 1)));
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_no_alarm_when_engine_not_running);
  RUN_TEST(test_coolant_edge_triggers_middle);
  RUN_TEST(test_oil_edge_triggers_middle);
  RUN_TEST(test_coolant_oil_repeat_is_rate_limited_and_repeats_after_interval);
  RUN_TEST(test_egt_edge_triggers_long);
  RUN_TEST(test_egt_repeat_is_rate_limited_and_repeats_after_interval);
  RUN_TEST(test_alarm_resets_after_temperature_returns_to_safe_range);
  RUN_TEST(test_engine_stop_resets_alarm_state);
  RUN_TEST(test_egt_has_priority_when_both_alarms_fire_together);

  return UNITY_END();
}
