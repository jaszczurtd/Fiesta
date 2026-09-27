#include "adjustometer_unit_testing.h"
#include "hal/impl/.mock/hal_mock.h"
#include "sensors.h"
#include "utils/unity.h"
#include <cmath>

static uint32_t sample(uint32_t hz, uint32_t us) {
  hal_mock_set_micros(us);
  adj_test_sensors_process_frequency(hz, us);
  adjustometer_feedback_t result = {};
  TEST_ASSERT_EQUAL(HAL_OK, getAdjustometerFeedback(&result));
  return result.filteredHz;
}

void setUp(void) {
  adj_test_sensors_reset_state();
  adj_sensors_test_state_t state = {};
  state.baselineReady = true;
  state.baseline = state.filteredHz = state.signalHz = 34000U;
  adj_test_sensors_set_state(&state);
}

void tearDown(void) {}

static void stepAt(uint32_t start) {
  TEST_ASSERT_EQUAL_UINT32(34000U, sample(34000U, start));
  TEST_ASSERT_UINT32_WITHIN(1U, 33870U, sample(32000U, start + 1000U));
  for (uint32_t t = 2000U; t <= 10000U; t += 1000U) {
    const uint32_t filtered = sample(32000U, start + t);
    TEST_ASSERT_TRUE(filtered >= 32000U && filtered <= 34000U);
  }
  TEST_ASSERT_EQUAL_UINT32(32000U, sample(32000U, start + 11000U));
}

void test_step_settles_in_one_period_without_overshoot(void) { stepAt(1U); }
void test_capture_clock_wrap_preserves_filter_history(void) {
  stepAt(UINT32_MAX - 3500U);
}

void test_pwm_ripple_is_rejected_with_irregular_capture_intervals(void) {
  const uint32_t dt[] = {870U, 1210U, 1000U, 940U, 1300U};
  uint32_t now = 1U;
  uint32_t maxError = 0U;
  for (size_t i = 0U; i < 1000U; ++i) {
    now += dt[i % COUNTOF(dt)];
    const float phase = 2.0f * 3.141592654f * 130.0f * (float)now * 1e-6f;
    const uint32_t filtered =
        sample((uint32_t)(34000.0f + 1000.0f * sinf(phase)), now);
    const uint32_t error = (uint32_t)abs((int)filtered - 34000);
    if (i > 20U && error > maxError)
      maxError = error;
  }
  TEST_ASSERT_LESS_THAN_UINT32(40U, maxError);
}

void test_motion_lags_by_half_window_instead_of_an_ema_tail(void) {
  sample(34000U, 1U);
  uint32_t filtered = 0U;
  for (uint32_t t = 1000U; t <= 20000U; t += 1000U) {
    filtered = sample(34000U - t / 50U, t + 1U);
  }
  // 20 kHz/s ramp: half of 7692 us is 76.92 Hz behind the current sample.
  TEST_ASSERT_UINT32_WITHIN(1U, 33677U, filtered);
}

void test_gap_and_reset_do_not_reuse_old_motion(void) {
  sample(34000U, 1U);
  sample(32000U, 1001U);
  TEST_ASSERT_EQUAL_UINT32(30000U, sample(30000U, 30001U));
  adj_test_sensors_reset_state();
  adj_sensors_test_state_t state = {};
  state.baselineReady = true;
  adj_test_sensors_set_state(&state);
  TEST_ASSERT_EQUAL_UINT32(35000U, sample(35000U, 31001U));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_step_settles_in_one_period_without_overshoot);
  RUN_TEST(test_capture_clock_wrap_preserves_filter_history);
  RUN_TEST(test_pwm_ripple_is_rejected_with_irregular_capture_intervals);
  RUN_TEST(test_motion_lags_by_half_window_instead_of_an_ema_tail);
  RUN_TEST(test_gap_and_reset_do_not_reuse_old_motion);
  return UNITY_END();
}
