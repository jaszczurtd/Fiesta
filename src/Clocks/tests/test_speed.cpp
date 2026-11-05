#include "hal/impl/.mock/hal_mock.h"
#include "speed.h"
#include "unity.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// The core-0 state of can.cpp, where speed.cpp stores the ABS speed.
static float s_globalValues[F_LAST];

void canSetValue(int idx, float val) {
  if ((idx >= 0) && (idx < F_LAST)) {
    s_globalValues[idx] = val;
  }
}

float canGetValue(int idx) {
  if ((idx >= 0) && (idx < F_LAST)) {
    return s_globalValues[idx];
  }
  return 0.0f;
}

static float getGlobalValue(int idx) { return canGetValue(idx); }

void setUp(void) {
  memset(s_globalValues, 0, sizeof(s_globalValues));
  hal_mock_serial_reset();
  hal_mock_set_millis(0);
}

void tearDown(void) {}

void test_calculateCircumferenceMeters_accepts_standard_tire_format(void) {
  double expectedSidewallHeight = (185.0 * (55.0 / 100.0)) / 1000.0;
  double expectedRimDiameter = 15.0 * 0.0254;
  double expectedDiameter =
      expectedRimDiameter + (2.0 * expectedSidewallHeight);
  double expectedCircumference = expectedDiameter * HAL_MATH_PI;

  TEST_ASSERT_TRUE(calculateCircumferenceMeters("185/55 R15", 1.0));
  TEST_ASSERT_TRUE(fabs(getCircumference() - expectedCircumference) < 0.0001);
}

void test_calculateCircumferenceMeters_rejects_invalid_format(void) {
  TEST_ASSERT_TRUE(calculateCircumferenceMeters("185/55 R15", 1.0));
  double previous = getCircumference();

  TEST_ASSERT_FALSE(calculateCircumferenceMeters("185-55 R15", 1.0));
  TEST_ASSERT_TRUE(fabs(getCircumference() - previous) < 0.0001);
}

void test_setupSpeedometer_interrupts_drive_abs_speed_updates(void) {
  TEST_ASSERT_TRUE(setupSpeedometer());

  for (int i = 0; i < 43; i++) {
    hal_mock_gpio_fire_interrupt(JH_HW_NODE_ABS_SPEED_PIN_IN);
  }

  hal_mock_set_millis(124);
  onImpulseTranslating();
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, getGlobalValue(F_ABS_CAR_SPEED));

  hal_mock_set_millis(125);
  onImpulseTranslating();

  TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)(getCircumference() * 8.0 * 3.6),
                           getGlobalValue(F_ABS_CAR_SPEED));
}

// The speed state is static, so every scenario first closes one window at a
// known time and counts from there.
static void startWindowAt(uint32_t nowMs) {
  hal_mock_set_millis(nowMs);
  onImpulseTranslating();
}

static void fireAbsImpulses(int count) {
  for (int i = 0; i < count; i++) {
    hal_mock_gpio_fire_interrupt(JH_HW_NODE_ABS_SPEED_PIN_IN);
  }
}

// Speed the current algorithm reports for impulses counted in one window:
// 43 impulses per wheel turn, always divided by the 125 ms window.
static float speedForWindowImpulses(int impulses) {
  return (float)(impulses / 43.0 / 0.125 * getCircumference() * 3.6);
}

void test_onImpulseTranslating_counts_only_impulses_of_the_current_window(
    void) {
  TEST_ASSERT_TRUE(setupSpeedometer());
  startWindowAt(1000u);

  fireAbsImpulses(43);
  hal_mock_set_millis(1125u);
  onImpulseTranslating();
  TEST_ASSERT_FLOAT_WITHIN(0.01f, speedForWindowImpulses(43),
                           getGlobalValue(F_ABS_CAR_SPEED));

  fireAbsImpulses(86);
  hal_mock_set_millis(1250u);
  onImpulseTranslating();
  TEST_ASSERT_FLOAT_WITHIN(0.01f, speedForWindowImpulses(86),
                           getGlobalValue(F_ABS_CAR_SPEED));
}

void test_onImpulseTranslating_reports_zero_without_impulses(void) {
  TEST_ASSERT_TRUE(setupSpeedometer());
  startWindowAt(2000u);

  fireAbsImpulses(43);
  hal_mock_set_millis(2125u);
  onImpulseTranslating();
  TEST_ASSERT_TRUE(getGlobalValue(F_ABS_CAR_SPEED) > 0.0f);

  hal_mock_set_millis(2250u);
  onImpulseTranslating();
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, getGlobalValue(F_ABS_CAR_SPEED));
}

void test_onImpulseTranslating_late_call_divides_by_the_fixed_window(void) {
  // Known error kept until the separate ABS/Speed stage: a call 250 ms after
  // the previous one counts 250 ms of impulses as if they came in 125 ms.
  TEST_ASSERT_TRUE(setupSpeedometer());
  startWindowAt(3000u);

  fireAbsImpulses(86); // a constant 43 impulses per 125 ms for 250 ms
  hal_mock_set_millis(3250u);
  onImpulseTranslating();
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f * speedForWindowImpulses(43),
                           getGlobalValue(F_ABS_CAR_SPEED));
}

static unsigned long expectPulseReport(float expectedHz, uint32_t pulses,
                                       uint32_t windowMs) {
  const char *report = strstr(hal_mock_serial_last_line(), "ABS pulses:");
  TEST_ASSERT_NOT_NULL(report);
  float hz = 0.0f;
  unsigned long delta = 0, duration = 0, total = 0;
  TEST_ASSERT_EQUAL_INT(
      4,
      sscanf(report, "ABS pulses: %f Hz, delta=%lu, window_ms=%lu, total=%lu",
             &hz, &delta, &duration, &total));
  TEST_ASSERT_FLOAT_WITHIN(0.005f, expectedHz, hz);
  TEST_ASSERT_EQUAL_UINT32(pulses, delta);
  TEST_ASSERT_EQUAL_UINT32(windowMs, duration);
  return total;
}

void test_pulse_frequency_reports_unchanged_input_and_actual_elapsed_time(
    void) {
  hal_mock_set_millis(5000u);
  TEST_ASSERT_TRUE(setupSpeedometer());

  fireAbsImpulses(43);
  hal_mock_set_millis(5999u);
  onImpulseTranslating();
  TEST_ASSERT_NULL(strstr(hal_mock_serial_last_line(), "ABS pulses:"));
  hal_mock_set_millis(6000u);
  onImpulseTranslating();
  const unsigned long firstTotal = expectPulseReport(43.0f, 43u, 1000u);

  fireAbsImpulses(43);
  hal_mock_set_millis(7000u);
  onImpulseTranslating();
  TEST_ASSERT_EQUAL_UINT32(firstTotal + 43u,
                           expectPulseReport(43.0f, 43u, 1000u));

  fireAbsImpulses(86);
  hal_mock_set_millis(9000u); // The loop missed its nominal reporting time.
  onImpulseTranslating();
  (void)expectPulseReport(43.0f, 86u, 2000u);
}

void test_pulse_frequency_reports_fractional_hz_and_zero_after_stop(void) {
  hal_mock_set_millis(10000u);
  TEST_ASSERT_TRUE(setupSpeedometer());
  fireAbsImpulses(1);
  hal_mock_set_millis(11250u);
  onImpulseTranslating();
  const unsigned long total = expectPulseReport(0.8f, 1u, 1250u);

  hal_mock_set_millis(12250u);
  onImpulseTranslating();
  TEST_ASSERT_EQUAL_UINT32(total, expectPulseReport(0.0f, 0u, 1000u));
}

void test_pulse_frequency_setup_starts_a_new_window_without_resetting_count(
    void) {
  hal_mock_set_millis(13000u);
  TEST_ASSERT_TRUE(setupSpeedometer());
  hal_mock_set_millis(14000u);
  onImpulseTranslating();
  const unsigned long total = expectPulseReport(0.0f, 0u, 1000u);

  fireAbsImpulses(7);
  hal_mock_set_millis(14500u);
  TEST_ASSERT_TRUE(setupSpeedometer());
  fireAbsImpulses(50);
  hal_mock_set_millis(15500u);
  onImpulseTranslating();
  TEST_ASSERT_EQUAL_UINT32(total + 57u, expectPulseReport(50.0f, 50u, 1000u));
}

void test_pulse_frequency_window_survives_millis_wrap(void) {
  hal_mock_set_millis(UINT32_MAX - 499u);
  TEST_ASSERT_TRUE(setupSpeedometer());
  fireAbsImpulses(100);
  hal_mock_set_millis(499u); // 999 ms since setup.
  onImpulseTranslating();
  TEST_ASSERT_NULL(strstr(hal_mock_serial_last_line(), "ABS pulses:"));
  hal_mock_set_millis(500u);
  onImpulseTranslating();
  (void)expectPulseReport(100.0f, 100u, 1000u);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_calculateCircumferenceMeters_accepts_standard_tire_format);
  RUN_TEST(test_calculateCircumferenceMeters_rejects_invalid_format);
  RUN_TEST(test_setupSpeedometer_interrupts_drive_abs_speed_updates);
  RUN_TEST(
      test_onImpulseTranslating_counts_only_impulses_of_the_current_window);
  RUN_TEST(test_onImpulseTranslating_reports_zero_without_impulses);
  RUN_TEST(test_onImpulseTranslating_late_call_divides_by_the_fixed_window);
  RUN_TEST(
      test_pulse_frequency_reports_unchanged_input_and_actual_elapsed_time);
  RUN_TEST(test_pulse_frequency_reports_fractional_hz_and_zero_after_stop);
  RUN_TEST(
      test_pulse_frequency_setup_starts_a_new_window_without_resetting_count);
  RUN_TEST(test_pulse_frequency_window_survives_millis_wrap);

  return UNITY_END();
}
