#include "config.h"
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

static uint16_t ampsToRaw(float amps) {
  return (uint16_t)((amps * VP37_CURRENT_SHUNT_OHMS *
                     (float)VP37_CURRENT_ADC_MAX_RAW / 3.3f) +
                    0.5f);
}

void test_pulse_analyze_guards_edges_and_winsorizes_a_switching_spike(void) {
  VP37CurrentPhaseSample samples[48] = {};
  const uint32_t cycleStartUs = 1000U;
  const uint32_t periodUs = 1000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ;
  const uint32_t sampleStepUs = periodUs / 160U;
  const uint32_t onTimeUs = sampleStepUs * COUNTOF(samples);
  for (uint32_t i = 0U; i < COUNTOF(samples); i++) {
    const float fraction = (float)i / (float)(COUNTOF(samples) - 1U);
    float amps = 3.0f + (2.0f * fraction);
    if ((i == 0U) || (i == 24U) || (i == COUNTOF(samples) - 1U)) {
      amps = 10.0f;
    }
    samples[i].timestampUs = cycleStartUs + (i * sampleStepUs);
    samples[i].rawSample = ampsToRaw(amps);
    samples[i].gateOn = 1U;
  }

  VP37CurrentPulseResult result;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_currentPulseAnalyze(samples, COUNTOF(samples), cycleStartUs,
                                       onTimeUs, periodUs, &result));
  TEST_ASSERT_TRUE(result.waveformValid);
  TEST_ASSERT_TRUE(result.guardedSamples < result.samples);
  TEST_ASSERT_GREATER_THAN_FLOAT(9.0f, result.peakAmps);
  TEST_ASSERT_LESS_THAN_FLOAT(5.2f, result.p95Amps);
  TEST_ASSERT_FLOAT_WITHIN(0.25f, 4.0f, result.meanAmps);
  const int32_t expectedPwm =
      (int32_t)(((uint64_t)onTimeUs * PWM_RESOLUTION) / periodUs);
  TEST_ASSERT_INT32_WITHIN(2, expectedPwm, result.pwmCommand);
}

void test_pulse_analyze_rejects_bad_timing_and_clipping(void) {
  VP37CurrentPhaseSample samples[16] = {};
  const uint32_t periodUs = 1000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ;
  const uint32_t onTimeUs = periodUs / 4U;
  for (uint32_t i = 0U; i < COUNTOF(samples); i++) {
    samples[i].timestampUs =
        1000U + 60U + (i * ((onTimeUs - 120U) / COUNTOF(samples)));
    samples[i].rawSample = ampsToRaw(4.0f);
    samples[i].gateOn = 1U;
  }

  VP37CurrentPulseResult result;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, VP37_currentPulseAnalyze(nullptr, COUNTOF(samples), 1000U,
                                           onTimeUs, periodUs, &result));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, VP37_currentPulseAnalyze(samples, COUNTOF(samples), 1000U,
                                           periodUs, periodUs, &result));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentPulseAnalyze(
                                    samples, COUNTOF(samples), 1000U, onTimeUs,
                                    (periodUs * 14U) / 10U, &result));
  TEST_ASSERT_FALSE(result.waveformValid);

  samples[8].clipped = 1U;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_currentPulseAnalyze(samples, COUNTOF(samples), 1000U,
                                       onTimeUs, periodUs, &result));
  TEST_ASSERT_EQUAL_UINT32(1U, result.clippedSamples);
  TEST_ASSERT_FALSE(result.waveformValid);
}

void test_pulse_analyze_handles_wrap_and_rejects_samples_outside_cycle(void) {
  VP37CurrentPhaseSample samples[12] = {};
  const uint32_t periodUs = 1000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ;
  const uint32_t onTimeUs = periodUs / 4U;
  const uint32_t sampleStepUs = (onTimeUs - 120U) / COUNTOF(samples);
  const uint32_t start = UINT32_MAX - 200U;
  for (uint32_t i = 0U; i < COUNTOF(samples); i++) {
    samples[i] = {start + 60U + i * sampleStepUs, ampsToRaw(6.5f), 1U, 0U};
  }
  samples[0].timestampUs = start - 1U;
  samples[1].timestampUs = start + onTimeUs + 1U;
  VP37CurrentPulseResult result;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_currentPulseAnalyze(samples, COUNTOF(samples), start,
                                       onTimeUs, periodUs, &result));
  TEST_ASSERT_EQUAL_UINT32(10U, result.guardedSamples);
  TEST_ASSERT_TRUE(result.waveformValid);
  // High current is an observation, without an arbitrary cutoff threshold.
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.5f, result.meanAmps);
  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW,
      VP37_currentPulseAnalyze(samples, VP37_CURRENT_PULSE_SAMPLES + 1U, start,
                               onTimeUs, periodUs, &result));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        VP37_currentPulseAnalyze(samples, 8U, start, onTimeUs,
                                                 periodUs, &result));
}

void test_capture_exposes_bad_zero_and_times_out_without_gate_edges(void) {
  VP37CurrentPulseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentSensePulseCapture(nullptr));
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 160);
  VP37_currentSenseInit();
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, VP37_currentSensePulseCapture(&result));
  TEST_ASSERT_FALSE(result.zeroValid);
  TEST_ASSERT_EQUAL_UINT16(160U, result.zeroRaw);
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 16);
  VP37_currentSenseInit();
  hal_mock_set_micros(UINT32_MAX - 10000U);
  const uint32_t started = hal_micros();
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT, VP37_currentSensePulseCapture(&result));
  TEST_ASSERT_TRUE(result.zeroValid);
  TEST_ASSERT_EQUAL_UINT16(16U, result.zeroRaw);
  TEST_ASSERT_UINT32_WITHIN(10U, 30000U, hal_micros() - started);
  TEST_ASSERT_FALSE(result.waveformValid);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pulse_analyze_guards_edges_and_winsorizes_a_switching_spike);
  RUN_TEST(test_pulse_analyze_rejects_bad_timing_and_clipping);
  RUN_TEST(test_pulse_analyze_handles_wrap_and_rejects_samples_outside_cycle);
  RUN_TEST(test_capture_exposes_bad_zero_and_times_out_without_gate_edges);
  return UNITY_END();
}
