#include "adjustometer_unit_testing.h"
#include "hal/impl/.mock/hal_mock.h"
#include "sensors.h"
#include "utils/unity.h"

static void edges(uint32_t count, uint32_t periodUs) {
  for (uint32_t i = 0; i < count; ++i) {
    hal_mock_advance_micros(periodUs);
    adj_test_sensors_count_edge();
  }
}

static adjustometer_feedback_t feedback(void) {
  adjustometer_feedback_t result = {};
  TEST_ASSERT_EQUAL(HAL_OK, getAdjustometerFeedback(&result));
  return result;
}

void setUp(void) {
  hal_mock_set_micros(1U);
  hal_mock_set_millis(0U);
  adj_test_sensors_reset_state();
  adj_sensors_test_state_t state = {};
  state.baselineReady = true;
  state.baseline = 10000U;
  state.filteredHz = 10000U;
  state.signalHz = 10000U;
  adj_test_sensors_set_state(&state);
}

void tearDown(void) {}

void test_window_requires_128_intervals_and_updates_every_32(void) {
  edges(128U, 100U);
  TEST_ASSERT_EQUAL_UINT32(0U, feedback().number);
  edges(1U, 100U);
  const adjustometer_feedback_t first = feedback();
  TEST_ASSERT_EQUAL_UINT32(10000U, first.rawHz);
  edges(31U, 100U);
  TEST_ASSERT_EQUAL_UINT32(first.number, feedback().number);
  edges(1U, 100U);
  const adjustometer_feedback_t next = feedback();
  TEST_ASSERT_EQUAL_UINT32(first.number + 1U, next.number);
  TEST_ASSERT_EQUAL_UINT32(3200U, next.measuredUs - first.measuredUs);
}

void test_step_uses_overlapping_history_without_96_period_bias(void) {
  edges(129U, 100U);
  const uint32_t expected[] = {11429U, 13333U, 16000U, 20000U};
  for (size_t i = 0; i < COUNTOF(expected); ++i) {
    edges(32U, 50U);
    TEST_ASSERT_EQUAL_UINT32(expected[i], feedback().rawHz);
  }
}

void test_clock_wrap_including_zero_timestamp_preserves_frequency(void) {
  hal_mock_set_micros(UINT32_MAX - 12899U);
  edges(129U, 100U);
  TEST_ASSERT_EQUAL_UINT32(0U, feedback().measuredUs);
  for (uint32_t i = 0; i < 6U; ++i) {
    edges(32U, 100U);
    TEST_ASSERT_EQUAL_UINT32(10000U, feedback().rawHz);
  }
}

void test_four_filter_updates_preserve_original_time_constant(void) {
  uint32_t filtered = 10000U;
  for (uint32_t i = 0; i < 4U; ++i) {
    filtered = adj_test_sensors_apply_adjustometer_ema(14000U, filtered);
  }
  TEST_ASSERT_UINT32_WITHIN(2U, 11000U, filtered);
}

void test_fractional_filter_converges_without_bias_or_stall(void) {
  uint32_t filtered = 10000U;
  for (uint32_t i = 0; i < 128U; ++i) {
    filtered = adj_test_sensors_apply_adjustometer_ema(10001U, filtered);
  }
  TEST_ASSERT_EQUAL_UINT32(10001U, filtered);
  for (uint32_t i = 0; i < 128U; ++i) {
    filtered = adj_test_sensors_apply_adjustometer_ema(10000U, filtered);
  }
  TEST_ASSERT_EQUAL_UINT32(10000U, filtered);
}

void test_reset_discards_old_window_and_reacquires_baseline(void) {
  edges(129U, 50U);
  adj_test_sensors_reset_state();
  TEST_ASSERT_FALSE(isAdjustometerReady());
  edges(128U * 110U, 100U);
  TEST_ASSERT_TRUE(isAdjustometerReady());
  edges(512U, 100U);
  TEST_ASSERT_EQUAL_UINT32(10000U, feedback().rawHz);
  TEST_ASSERT_INT32_WITHIN(5, 0, getAdjustometerPulses());
}

void test_missing_signal_still_invalidates_feedback(void) {
  edges(129U, 100U);
  hal_mock_advance_micros(210000U);
  TEST_ASSERT_TRUE((feedback().status & ADJ_STATUS_SIGNAL_LOST) != 0U);
}

void test_zero_release_requires_eight_short_updates(void) {
  adj_sensors_test_state_t state = {};
  adj_test_sensors_get_state(&state);
  state.baseline = 9800U;
  state.zeroHold = true;
  adj_test_sensors_set_state(&state);
  edges(129U + 6U * 32U, 100U);
  TEST_ASSERT_EQUAL_INT32(0, getAdjustometerPulses());
  edges(32U, 100U);
  TEST_ASSERT_EQUAL_INT32(200, getAdjustometerPulses());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_window_requires_128_intervals_and_updates_every_32);
  RUN_TEST(test_step_uses_overlapping_history_without_96_period_bias);
  RUN_TEST(test_clock_wrap_including_zero_timestamp_preserves_frequency);
  RUN_TEST(test_four_filter_updates_preserve_original_time_constant);
  RUN_TEST(test_fractional_filter_converges_without_bias_or_stall);
  RUN_TEST(test_reset_discards_old_window_and_reacquires_baseline);
  RUN_TEST(test_missing_signal_still_invalidates_feedback);
  RUN_TEST(test_zero_release_requires_eight_short_updates);
  return UNITY_END();
}
