#include "hal/impl/.mock/hal_mock.h"
#include "hardwareConfig.h"
#include "unity.h"
#include "vp37_current.h"

void setUp(void) {
  hal_mock_set_micros(0U);
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 0);
  VP37_currentSenseInit();
}

void tearDown(void) {}

void test_snapshot_requires_output_and_samples(void) {
  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentSenseSnapshot(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, VP37_currentSenseSnapshot(&reading));
}

void test_zero_current_window_remains_zero(void) {
  VP37_currentSenseSample();
  VP37_currentSenseSample();

  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseSnapshot(&reading));
  TEST_ASSERT_EQUAL_UINT32(2U, reading.samples);
  TEST_ASSERT_EQUAL_UINT32(0U, reading.activeSamples);
  TEST_ASSERT_EQUAL_UINT16(0U, reading.rawMin);
  TEST_ASSERT_EQUAL_UINT16(0U, reading.rawMax);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.peakAmps);
}

void test_source_shunt_voltage_converts_to_on_current(void) {
  // RP2040 DNL compensation maps 1600 to 1616 ADC counts.
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 1600);
  for (uint32_t i = 0U; i < 8U; i++) {
    VP37_currentSenseSample();
  }

  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseSnapshot(&reading));
  TEST_ASSERT_EQUAL_UINT16(1616U, reading.rawMin);
  TEST_ASSERT_EQUAL_UINT16(1616U, reading.rawMax);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.3025f, reading.peakVolts);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 5.920f, reading.peakAmps);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 5.920f, reading.activeMeanAmps);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 5.920f, reading.p95Amps);
}

void test_startup_median_is_subtracted_from_current_samples(void) {
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 16);
  VP37_currentSenseInit();
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 1600);
  for (uint32_t i = 0U; i < 8U; i++) {
    VP37_currentSenseSample();
  }

  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseSnapshot(&reading));
  TEST_ASSERT_EQUAL_UINT16(16U, reading.zeroRaw);
  TEST_ASSERT_TRUE(reading.zeroValid);
  TEST_ASSERT_EQUAL_UINT16(1600U, reading.rawMin);
  TEST_ASSERT_EQUAL_UINT16(1600U, reading.rawMax);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 5.86f, reading.activeMeanAmps);
}

void test_implausibly_high_startup_zero_is_exposed_and_not_subtracted(void) {
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 160);
  VP37_currentSenseInit();
  for (uint32_t i = 0U; i < 8U; i++) {
    VP37_currentSenseSample();
  }

  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseSnapshot(&reading));
  TEST_ASSERT_EQUAL_UINT16(160U, reading.zeroRaw);
  TEST_ASSERT_FALSE(reading.zeroValid);
  TEST_ASSERT_EQUAL_UINT16(160U, reading.rawMin);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.switchMeanAmps);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.activeMeanAmps);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.p95Amps);
}

void test_activity_threshold_is_applied_after_zero_subtraction(void) {
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 16);
  VP37_currentSenseInit();

  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 79);
  VP37_currentSenseSample();
  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseSnapshot(&reading));
  TEST_ASSERT_EQUAL_UINT32(0U, reading.activeSamples);

  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 80);
  for (uint32_t i = 0U; i < 8U; i++) {
    VP37_currentSenseSample();
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseSnapshot(&reading));
  TEST_ASSERT_EQUAL_UINT32(8U, reading.activeSamples);
  TEST_ASSERT_EQUAL_UINT32(8U, reading.maxActiveRun);
}

void test_burst_capture_covers_twenty_five_milliseconds(void) {
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 1600);
  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseCapture(&reading));
  TEST_ASSERT_EQUAL_UINT32(1250U, reading.samples);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(25000U, reading.windowUs);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 5.920f, reading.p95Amps);
}

void test_active_fraction_and_switch_mean_include_pwm_off_samples(void) {
  for (uint32_t i = 0U; i < 8U; i++) {
    VP37_currentSenseSample();
  }
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 1600);
  for (uint32_t i = 0U; i < 8U; i++) {
    VP37_currentSenseSample();
  }

  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseSnapshot(&reading));
  TEST_ASSERT_EQUAL_UINT32(16U, reading.samples);
  TEST_ASSERT_EQUAL_UINT32(8U, reading.activeSamples);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, reading.activePercent);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 2.960f, reading.switchMeanAmps);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 5.920f, reading.activeMeanAmps);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, VP37_currentSenseSnapshot(&reading));
}

void test_isolated_active_samples_are_reported_as_spikes(void) {
  for (uint32_t spike = 0U; spike < 8U; spike++) {
    hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 1600);
    VP37_currentSenseSample();
    hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 0);
    for (uint32_t i = 0U; i < 155U; i++) {
      VP37_currentSenseSample();
    }
  }
  for (uint32_t i = 0U; i < 2U; i++) {
    VP37_currentSenseSample();
  }

  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseSnapshot(&reading));
  TEST_ASSERT_EQUAL_UINT32(1250U, reading.samples);
  TEST_ASSERT_EQUAL_UINT32(8U, reading.activeSamples);
  TEST_ASSERT_EQUAL_UINT32(1U, reading.maxActiveRun);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.64f, reading.activePercent);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.switchMeanAmps);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.activeMeanAmps);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, reading.p95Amps);
  TEST_ASSERT_GREATER_THAN_FLOAT(5.0f, reading.peakAmps);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_snapshot_requires_output_and_samples);
  RUN_TEST(test_zero_current_window_remains_zero);
  RUN_TEST(test_source_shunt_voltage_converts_to_on_current);
  RUN_TEST(test_startup_median_is_subtracted_from_current_samples);
  RUN_TEST(test_implausibly_high_startup_zero_is_exposed_and_not_subtracted);
  RUN_TEST(test_activity_threshold_is_applied_after_zero_subtraction);
  RUN_TEST(test_active_fraction_and_switch_mean_include_pwm_off_samples);
  RUN_TEST(test_isolated_active_samples_are_reported_as_spikes);
  RUN_TEST(test_burst_capture_covers_twenty_five_milliseconds);
  return UNITY_END();
}
