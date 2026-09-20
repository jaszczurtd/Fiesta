#include "engineMaps.h"
#include "unity.h"
#include "vp37.h"

void setUp(void) {}
void tearDown(void) {}

// The interpolation in vp37.c walks the knots upward and stops at the first
// one at or above the demand, so the axis must climb strictly from 0 to 100.
void test_vp37_feedforward_axis_spans_the_stroke_in_ascending_knots(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_FF_MAP[0U][VP37_FF_COL_PERCENT]);
  TEST_ASSERT_EQUAL_FLOAT(100.0f,
                          VP37_FF_MAP[VP37_FF_KNOTS - 1U][VP37_FF_COL_PERCENT]);
  for (size_t i = 1U; i < VP37_FF_KNOTS; i++) {
    TEST_ASSERT_GREATER_THAN_FLOAT(VP37_FF_MAP[i - 1U][VP37_FF_COL_PERCENT],
                                   VP37_FF_MAP[i][VP37_FF_COL_PERCENT]);
  }
}

// A holding command that fell with demand, or a negative motion term, would
// be a typo, not a calibration.
void test_vp37_feedforward_columns_never_fall_with_demand(void) {
  for (size_t i = 1U; i < VP37_FF_KNOTS; i++) {
    TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(VP37_FF_MAP[i - 1U][VP37_FF_COL_PWM],
                                       VP37_FF_MAP[i][VP37_FF_COL_PWM]);
    TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(VP37_FF_MAP[i - 1U][VP37_FF_COL_MOTION],
                                       VP37_FF_MAP[i][VP37_FF_COL_MOTION]);
  }
  TEST_ASSERT_EQUAL_FLOAT(0.0f, VP37_FF_MAP[0U][VP37_FF_COL_MOTION]);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, VP37_PWM_FF_MOTION_BOOST);
  TEST_ASSERT_GREATER_THAN_FLOAT(VP37_PWM_FF_AT_MIN, VP37_PWM_FF_AT_MAX);
}

// Both stroke tapers share the axis and the taper start; authority eases off
// toward the top of the stroke while the dead zone widens there.
void test_stroke_tapers_share_the_axis_and_bend_the_right_way(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f,
                          VP37_INTEGRAL_LIMIT_MAP[0U][VP37_TAPER_COL_PERCENT]);
  TEST_ASSERT_EQUAL_FLOAT(100.0f,
                          VP37_INTEGRAL_LIMIT_MAP[VP37_STROKE_TAPER_KNOTS - 1U]
                                                 [VP37_TAPER_COL_PERCENT]);
  for (size_t i = 0U; i < VP37_STROKE_TAPER_KNOTS; i++) {
    TEST_ASSERT_EQUAL_FLOAT(
        VP37_INTEGRAL_LIMIT_MAP[i][VP37_TAPER_COL_PERCENT],
        VP37_INTEGRAL_DEADBAND_MAP[i][VP37_TAPER_COL_PERCENT]);
    if (i > 0U) {
      TEST_ASSERT_GREATER_THAN_FLOAT(
          VP37_INTEGRAL_LIMIT_MAP[i - 1U][VP37_TAPER_COL_PERCENT],
          VP37_INTEGRAL_LIMIT_MAP[i][VP37_TAPER_COL_PERCENT]);
      TEST_ASSERT_LESS_OR_EQUAL_FLOAT(
          VP37_INTEGRAL_LIMIT_MAP[i - 1U][VP37_TAPER_COL_VALUE],
          VP37_INTEGRAL_LIMIT_MAP[i][VP37_TAPER_COL_VALUE]);
      TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(
          VP37_INTEGRAL_DEADBAND_MAP[i - 1U][VP37_TAPER_COL_VALUE],
          VP37_INTEGRAL_DEADBAND_MAP[i][VP37_TAPER_COL_VALUE]);
    }
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(VP37_PID_DEADBAND, VP37_PID_DEADBAND_TOP_HZ);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, VP37_PID_TRIM_PWM);
}

// The proportional gain only ever grows toward the top, from exactly the base
// gain: a multiplier below one would soften the loop where it has no margin.
void test_vp37_proportional_gain_map_starts_at_unity_and_never_falls(void) {
  TEST_ASSERT_EQUAL_FLOAT(
      0.0f, VP37_PROPORTIONAL_GAIN_MAP[0U][VP37_TAPER_COL_PERCENT]);
  TEST_ASSERT_EQUAL_FLOAT(1.0f,
                          VP37_PROPORTIONAL_GAIN_MAP[0U][VP37_TAPER_COL_VALUE]);
  for (size_t i = 1U; i < VP37_STROKE_TAPER_KNOTS; i++) {
    TEST_ASSERT_GREATER_THAN_FLOAT(
        VP37_PROPORTIONAL_GAIN_MAP[i - 1U][VP37_TAPER_COL_PERCENT],
        VP37_PROPORTIONAL_GAIN_MAP[i][VP37_TAPER_COL_PERCENT]);
    TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(
        VP37_PROPORTIONAL_GAIN_MAP[i - 1U][VP37_TAPER_COL_VALUE],
        VP37_PROPORTIONAL_GAIN_MAP[i][VP37_TAPER_COL_VALUE]);
  }
  TEST_ASSERT_LESS_OR_EQUAL_FLOAT(
      100.0f, VP37_PROPORTIONAL_GAIN_MAP[VP37_STROKE_TAPER_KNOTS - 1U]
                                        [VP37_TAPER_COL_PERCENT]);
}

// The error limit closes toward the top and stays wider than the integration
// dead zone there; a limit inside the dead zone would switch the integral off.
void test_vp37_error_limit_map_closes_toward_the_top_above_the_dead_zone(void) {
  TEST_ASSERT_EQUAL_FLOAT(
      0.0f, VP37_PROPORTIONAL_ERROR_LIMIT_MAP[0U][VP37_TAPER_COL_PERCENT]);
  for (size_t i = 1U; i < VP37_STROKE_TAPER_KNOTS; i++) {
    TEST_ASSERT_GREATER_THAN_FLOAT(
        VP37_PROPORTIONAL_ERROR_LIMIT_MAP[i - 1U][VP37_TAPER_COL_PERCENT],
        VP37_PROPORTIONAL_ERROR_LIMIT_MAP[i][VP37_TAPER_COL_PERCENT]);
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(
        VP37_PROPORTIONAL_ERROR_LIMIT_MAP[i - 1U][VP37_TAPER_COL_VALUE],
        VP37_PROPORTIONAL_ERROR_LIMIT_MAP[i][VP37_TAPER_COL_VALUE]);
  }
  TEST_ASSERT_GREATER_THAN_FLOAT(
      VP37_PID_DEADBAND_TOP_HZ,
      VP37_PROPORTIONAL_ERROR_LIMIT_MAP[VP37_STROKE_TAPER_KNOTS - 1U]
                                       [VP37_TAPER_COL_VALUE]);
}

// The feedback lead is a share: nothing across the lower stroke, never more
// than the whole filter lag, never falling toward the top.
void test_vp37_feedback_lead_map_is_a_share_that_starts_at_zero(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f,
                          VP37_FEEDBACK_LEAD_MAP[0U][VP37_TAPER_COL_PERCENT]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f,
                          VP37_FEEDBACK_LEAD_MAP[0U][VP37_TAPER_COL_VALUE]);
  for (size_t i = 1U; i < VP37_STROKE_TAPER_KNOTS; i++) {
    TEST_ASSERT_GREATER_THAN_FLOAT(
        VP37_FEEDBACK_LEAD_MAP[i - 1U][VP37_TAPER_COL_PERCENT],
        VP37_FEEDBACK_LEAD_MAP[i][VP37_TAPER_COL_PERCENT]);
    TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(
        VP37_FEEDBACK_LEAD_MAP[i - 1U][VP37_TAPER_COL_VALUE],
        VP37_FEEDBACK_LEAD_MAP[i][VP37_TAPER_COL_VALUE]);
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(
        1.0f, VP37_FEEDBACK_LEAD_MAP[i][VP37_TAPER_COL_VALUE]);
  }
}

// N75 duty is a percentage that eases off with more pedal and more speed; a
// row or column climbing back up would be a typo.
void test_n75_table_holds_percentages_that_ease_off_with_pedal_and_speed(void) {
  for (int row = 0; row < RPM_PRESCALERS; row++) {
    for (int col = 0; col < N75_PERCENT_VALS; col++) {
      TEST_ASSERT_TRUE(RPM_table[row][col] >= 0);
      TEST_ASSERT_TRUE(RPM_table[row][col] <= 100);
      if (col > 0) {
        TEST_ASSERT_TRUE(RPM_table[row][col] <= RPM_table[row][col - 1]);
      }
      if (row > 0) {
        TEST_ASSERT_TRUE(RPM_table[row][col] <= RPM_table[row - 1][col]);
      }
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_vp37_feedforward_axis_spans_the_stroke_in_ascending_knots);
  RUN_TEST(test_vp37_feedforward_columns_never_fall_with_demand);
  RUN_TEST(test_stroke_tapers_share_the_axis_and_bend_the_right_way);
  RUN_TEST(test_vp37_proportional_gain_map_starts_at_unity_and_never_falls);
  RUN_TEST(test_vp37_error_limit_map_closes_toward_the_top_above_the_dead_zone);
  RUN_TEST(test_vp37_feedback_lead_map_is_a_share_that_starts_at_zero);
  RUN_TEST(test_n75_table_holds_percentages_that_ease_off_with_pedal_and_speed);
  return UNITY_END();
}
