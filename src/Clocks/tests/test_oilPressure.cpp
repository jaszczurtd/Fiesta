#include "hal/impl/.mock/hal_mock.h"
#include "oilPressure.h"
#include "unity.h"

#include <math.h>

static const int kAdcMax = (1 << OIL_PRESSURE_ADC_BITS) - 1;

// ADC sample of the pull-up divider with a sender of @p ohm.
static int sampleForResistance(float ohm) {
  return (int)lroundf((float)kAdcMax * ohm / (OIL_PRESSURE_PULLUP_OHM + ohm));
}

static oil_sender_state_t classify(float ohm, float *bar) {
  return oilPressureFromSample(sampleForResistance(ohm), bar);
}

void setUp(void) { TEST_ASSERT_TRUE(setupOilPressure()); }

void tearDown(void) {}

void test_sender_range_maps_linearly_to_pressure(void) {
  float bar = -1.0f;
  TEST_ASSERT_EQUAL_INT(OIL_SENDER_OK,
                        classify(OIL_PRESSURE_SENSOR_RES_MIN_OHM, &bar));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, bar);

  const float middle =
      (OIL_PRESSURE_SENSOR_RES_MIN_OHM + OIL_PRESSURE_SENSOR_RES_MAX_OHM) / 2;
  TEST_ASSERT_EQUAL_INT(OIL_SENDER_OK, classify(middle, &bar));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, OIL_PRESSURE_MAX_BAR / 2.0f, bar);

  TEST_ASSERT_EQUAL_INT(OIL_SENDER_OK,
                        classify(OIL_PRESSURE_SENSOR_RES_MAX_OHM, &bar));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, OIL_PRESSURE_MAX_BAR, bar);
}

void test_short_and_open_limits_separate_faults_from_readings(void) {
  float bar = -1.0f;
  TEST_ASSERT_EQUAL_INT(OIL_SENDER_OK,
                        classify(OIL_PRESSURE_SHORT_OHM * 1.2f, &bar));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, bar);
  TEST_ASSERT_EQUAL_INT(OIL_SENDER_SHORT,
                        classify(OIL_PRESSURE_SHORT_OHM * 0.8f, &bar));

  TEST_ASSERT_EQUAL_INT(OIL_SENDER_OK,
                        classify(OIL_PRESSURE_OPEN_OHM * 0.9f, &bar));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, OIL_PRESSURE_MAX_BAR, bar);
  TEST_ASSERT_EQUAL_INT(OIL_SENDER_OPEN,
                        classify(OIL_PRESSURE_OPEN_OHM * 1.1f, &bar));
}

void test_adc_at_full_scale_is_an_open_sender_not_maximum_pressure(void) {
  float bar = -1.0f;
  TEST_ASSERT_EQUAL_INT(OIL_SENDER_OPEN, oilPressureFromSample(kAdcMax, &bar));
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, bar);
  TEST_ASSERT_EQUAL_INT(OIL_SENDER_SHORT, oilPressureFromSample(0, &bar));
  TEST_ASSERT_EQUAL_INT(OIL_SENDER_NO_SAMPLE, oilPressureFromSample(-1, &bar));
}

void test_reading_filters_with_alpha(void) {
  hal_mock_adc_inject(JH_HW_NODE_OIL_PRESSURE_PIN_IN,
                      sampleForResistance(OIL_PRESSURE_SENSOR_RES_MAX_OHM));
  float filtered = -1.0f;

  TEST_ASSERT_EQUAL_INT(OIL_SENDER_OK, readOilPressure(&filtered));
  TEST_ASSERT_FLOAT_WITHIN(
      0.05f, OIL_PRESSURE_MAX_BAR * OIL_PRESSURE_FILTER_ALPHA, filtered);

  TEST_ASSERT_EQUAL_INT(OIL_SENDER_OK, readOilPressure(&filtered));
  const float second = OIL_PRESSURE_MAX_BAR * OIL_PRESSURE_FILTER_ALPHA *
                       (2.0f - OIL_PRESSURE_FILTER_ALPHA);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, second, filtered);
}

void test_faulty_sample_leaves_the_filter_unchanged(void) {
  hal_mock_adc_inject(JH_HW_NODE_OIL_PRESSURE_PIN_IN,
                      sampleForResistance(OIL_PRESSURE_SENSOR_RES_MAX_OHM));
  float filtered = -1.0f;
  TEST_ASSERT_EQUAL_INT(OIL_SENDER_OK, readOilPressure(&filtered));
  const float before = filtered;

  hal_mock_adc_inject(JH_HW_NODE_OIL_PRESSURE_PIN_IN, kAdcMax);
  TEST_ASSERT_EQUAL_INT(OIL_SENDER_OPEN, readOilPressure(&filtered));
  TEST_ASSERT_EQUAL_FLOAT(before, filtered);

  hal_mock_adc_inject(JH_HW_NODE_OIL_PRESSURE_PIN_IN,
                      sampleForResistance(OIL_PRESSURE_SENSOR_RES_MAX_OHM));
  TEST_ASSERT_EQUAL_INT(OIL_SENDER_OK, readOilPressure(&filtered));
  TEST_ASSERT_TRUE(filtered > before);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_sender_range_maps_linearly_to_pressure);
  RUN_TEST(test_short_and_open_limits_separate_faults_from_readings);
  RUN_TEST(test_adc_at_full_scale_is_an_open_sender_not_maximum_pressure);
  RUN_TEST(test_reading_filters_with_alpha);
  RUN_TEST(test_faulty_sample_leaves_the_filter_unchanged);
  return UNITY_END();
}
