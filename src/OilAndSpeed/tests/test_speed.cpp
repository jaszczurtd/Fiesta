#include "unity.h"
#include "speed.h"
#include "hal/impl/.mock/hal_mock.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

static float s_globalValues[F_LAST];

extern "C" int parseNumber(const char **str) {
  int value = 0;

  if ((str == NULL) || (*str == NULL)) {
    return 0;
  }

  while (isdigit((unsigned char)**str)) {
    value = (value * 10) + (**str - '0');
    (*str)++;
  }

  return value;
}

extern "C" void removeSpaces(char *str) {
  char *dst = str;

  if (str == NULL) {
    return;
  }

  for (char *src = str; *src != '\0'; ++src) {
    if (!isspace((unsigned char)*src)) {
      *dst++ = *src;
    }
  }

  *dst = '\0';
}

void setGlobalValue(int idx, float val) {
  if ((idx >= 0) && (idx < F_LAST)) {
    s_globalValues[idx] = val;
  }
}

float getGlobalValue(int idx) {
  if ((idx >= 0) && (idx < F_LAST)) {
    return s_globalValues[idx];
  }
  return 0.0f;
}

void setUp(void) {
  memset(s_globalValues, 0, sizeof(s_globalValues));
  hal_mock_serial_reset();
  hal_mock_set_millis(0);
}

void tearDown(void) {}

void test_calculateCircumferenceMeters_accepts_standard_tire_format(void) {
  double expectedSidewallHeight = (185.0 * (55.0 / 100.0)) / 1000.0;
  double expectedRimDiameter = 15.0 * 0.0254;
  double expectedDiameter = expectedRimDiameter + (2.0 * expectedSidewallHeight);
  double expectedCircumference = expectedDiameter * PI;

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
    hal_mock_gpio_fire_interrupt(ABS_INPUT_PIN);
  }

  hal_mock_set_millis(124);
  onImpulseTranslating();
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, getGlobalValue(F_ABS_CAR_SPEED));

  hal_mock_set_millis(125);
  onImpulseTranslating();

  TEST_ASSERT_FLOAT_WITHIN(0.01f,
                           (float)(getCircumference() * 8.0 * 3.6),
                           getGlobalValue(F_ABS_CAR_SPEED));
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_calculateCircumferenceMeters_accepts_standard_tire_format);
  RUN_TEST(test_calculateCircumferenceMeters_rejects_invalid_format);
  RUN_TEST(test_setupSpeedometer_interrupts_drive_abs_speed_updates);

  return UNITY_END();
}