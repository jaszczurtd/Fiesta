#include "hal/impl/.mock/hal_mock.h"
#include "speed.h"
#include "unity.h"

#include <math.h>
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

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_calculateCircumferenceMeters_accepts_standard_tire_format);
  RUN_TEST(test_calculateCircumferenceMeters_rejects_invalid_format);
  RUN_TEST(test_setupSpeedometer_interrupts_drive_abs_speed_updates);
  RUN_TEST(
      test_onImpulseTranslating_counts_only_impulses_of_the_current_window);
  RUN_TEST(test_onImpulseTranslating_reports_zero_without_impulses);
  RUN_TEST(test_onImpulseTranslating_late_call_divides_by_the_fixed_window);

  return UNITY_END();
}
