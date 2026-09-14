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

// ── Percentiles, clipping, RMS and run plausibility ──────────────────────────

/** ADC codes per ampere on the 0.22 ohm shunt with a 3.3 V, 12-bit reference.
 */
static uint16_t ampsToRaw(float amps) {
  return (uint16_t)((amps * VP37_CURRENT_SHUNT_OHMS *
                     (float)VP37_CURRENT_ADC_MAX_RAW / 3.3f) +
                    0.5f);
}

/** Feed one PWM period: a linear ON ramp from startAmps to endAmps, then zero.
 */
static void feedPeriod(float startAmps, float endAmps, uint32_t onSamples,
                       uint32_t offSamples) {
  for (uint32_t i = 0U; i < onSamples; i++) {
    const float fraction =
        (onSamples > 1U) ? ((float)i / (float)(onSamples - 1U)) : 0.0f;
    const float amps = startAmps + ((endAmps - startAmps) * fraction);
    hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, (int)ampsToRaw(amps));
    VP37_currentSenseSample();
  }
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 0);
  for (uint32_t i = 0U; i < offSamples; i++) {
    VP37_currentSenseSample();
  }
}

void test_histogram_percentile_rejects_bad_arguments(void) {
  uint16_t bin = 0U;
  const uint16_t histogram[4] = {1U, 1U, 1U, 1U};
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentHistogramPercentile(
                                        nullptr, 4U, 0U, 4U, 50U, &bin));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentHistogramPercentile(
                                        histogram, 4U, 0U, 4U, 50U, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentHistogramPercentile(
                                        histogram, 4U, 0U, 0U, 50U, &bin));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentHistogramPercentile(
                                        histogram, 4U, 4U, 4U, 50U, &bin));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentHistogramPercentile(
                                        histogram, 4U, 0U, 4U, 0U, &bin));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentHistogramPercentile(
                                        histogram, 4U, 0U, 4U, 101U, &bin));
}

void test_histogram_percentile_walks_from_the_requested_bin(void) {
  uint16_t histogram[8] = {50U, 0U, 10U, 10U, 10U, 10U, 10U, 0U};
  uint16_t bin = 0U;

  // Counting everything, the median sits inside the large low bin.
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentHistogramPercentile(
                                    histogram, 8U, 0U, 100U, 50U, &bin));
  TEST_ASSERT_EQUAL_UINT16(0U, bin);

  // Skipping the low bin, the same histogram spreads evenly over bins 2..6.
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentHistogramPercentile(
                                    histogram, 8U, 2U, 50U, 50U, &bin));
  TEST_ASSERT_EQUAL_UINT16(4U, bin);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentHistogramPercentile(
                                    histogram, 8U, 2U, 50U, 5U, &bin));
  TEST_ASSERT_EQUAL_UINT16(2U, bin);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentHistogramPercentile(
                                    histogram, 8U, 2U, 50U, 95U, &bin));
  TEST_ASSERT_EQUAL_UINT16(6U, bin);
}

void test_expected_active_run_follows_the_commanded_duty(void) {
  // 1250 samples over 38 ms is about 164 samples per 5 ms PWM period.
  const uint32_t samples = 1250U;
  const uint32_t windowUs = 38000U;
  TEST_ASSERT_EQUAL_UINT32(
      0U, VP37_currentExpectedActiveRun(-1, samples, windowUs));
  TEST_ASSERT_EQUAL_UINT32(0U,
                           VP37_currentExpectedActiveRun(691, 0U, windowUs));
  TEST_ASSERT_EQUAL_UINT32(0U, VP37_currentExpectedActiveRun(691, samples, 0U));

  // Samples per PWM period follow the configured actuator frequency, which
  // differs between the VP37 firmware and this host build.
  const float periodUs = 1000000.0f / (float)VP37_PWM_FREQUENCY_HZ;
  const float samplesPerPeriod = ((float)samples * periodUs) / (float)windowUs;

  const uint32_t run = VP37_currentExpectedActiveRun(691, samples, windowUs);
  TEST_ASSERT_UINT32_WITHIN(
      2U, (uint32_t)(samplesPerPeriod * 691.0f / (float)PWM_RESOLUTION), run);

  // A command above full scale saturates instead of overflowing.
  const uint32_t full =
      VP37_currentExpectedActiveRun(PWM_RESOLUTION * 4, samples, windowUs);
  TEST_ASSERT_UINT32_WITHIN(2U, (uint32_t)samplesPerPeriod, full);
}

void test_percentiles_describe_the_on_phase_shape(void) {
  // A ramp spreads the active samples evenly between its end points.
  feedPeriod(1.0f, 5.0f, 60U, 104U);

  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseSnapshot(&reading));
  TEST_ASSERT_TRUE(reading.rawP05 <= reading.rawP25);
  TEST_ASSERT_TRUE(reading.rawP25 <= reading.rawP50);
  TEST_ASSERT_TRUE(reading.rawP50 <= reading.rawP75);
  TEST_ASSERT_TRUE(reading.rawP75 <= reading.rawP95);
  TEST_ASSERT_FLOAT_WITHIN(0.25f, 1.0f, reading.p05Amps);
  TEST_ASSERT_FLOAT_WITHIN(0.25f, 3.0f, reading.p50Amps);
  TEST_ASSERT_FLOAT_WITHIN(0.25f, 5.0f, reading.p95Amps);

  // A flat top bunches every percentile against the plateau.
  feedPeriod(5.0f, 5.0f, 60U, 104U);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseSnapshot(&reading));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, reading.p05Amps);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, reading.p95Amps);
}

void test_clipped_samples_are_counted(void) {
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, (int)VP37_CURRENT_ADC_MAX_RAW);
  for (uint32_t i = 0U; i < 8U; i++) {
    VP37_currentSenseSample();
  }
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 0);
  for (uint32_t i = 0U; i < 8U; i++) {
    VP37_currentSenseSample();
  }

  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseSnapshot(&reading));
  TEST_ASSERT_EQUAL_UINT32(8U, reading.clippedSamples);
}

void test_shunt_rms_and_power_use_the_mean_square(void) {
  // Half the window at 5.92 A, half at zero: RMS is 5.92/sqrt(2).
  for (uint32_t i = 0U; i < 8U; i++) {
    VP37_currentSenseSample();
  }
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 1600);
  for (uint32_t i = 0U; i < 8U; i++) {
    VP37_currentSenseSample();
  }

  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentSenseSnapshot(&reading));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 4.186f, reading.rmsShuntAmps);
  // The square mean, not the square of the mean: 4.186^2 * 0.22 = 3.85 W.
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 3.855f, reading.shuntPowerWatts);
  TEST_ASSERT_TRUE(reading.shuntPowerWatts >
                   (reading.switchMeanAmps * reading.switchMeanAmps *
                    VP37_CURRENT_SHUNT_OHMS));
}

void test_run_plausibility_rejects_a_fragmented_window(void) {
  // Four-sample bursts pass the old validity gate but cannot be an ON phase at
  // this duty.
  for (uint32_t burst = 0U; burst < 40U; burst++) {
    hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 1365);
    for (uint32_t i = 0U; i < 4U; i++) {
      VP37_currentSenseSample();
    }
    hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 0);
    for (uint32_t i = 0U; i < 8U; i++) {
      VP37_currentSenseSample();
    }
  }
  hal_mock_advance_micros(38000U);

  VP37CurrentConditions conditions = {};
  conditions.pwmCommand = 691;
  conditions.measuredHz = 6847;
  conditions.desiredHz = 8196;
  conditions.supplyVolts = 14.6f;
  conditions.fuelTempC = 26.0f;

  VP37CurrentReading reading;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_currentSenseSnapshotEx(&conditions, &reading));
  TEST_ASSERT_EQUAL_UINT32(4U, reading.maxActiveRun);
  TEST_ASSERT_TRUE(reading.expectedActiveRun > 10U);
  TEST_ASSERT_FALSE(reading.activeRunPlausible);
  // The conditions travel with the window so the log is self-describing.
  TEST_ASSERT_EQUAL_INT32(691, reading.conditions.pwmCommand);
  TEST_ASSERT_EQUAL_INT32(6847, reading.conditions.measuredHz);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.6f, reading.conditions.supplyVolts);
}

// ── Phase-aligned analysis ───────────────────────────────────────────────────

/** Build a synthetic capture: linear ON ramp, zero during the OFF phase. */
static uint32_t buildPhaseCapture(VP37CurrentPhaseSample *out,
                                  uint32_t capacity, float startAmps,
                                  float endAmps, uint32_t onSamples,
                                  uint32_t periodSamples, uint32_t stepUs) {
  uint32_t count = 0U;
  uint32_t timestamp = 0U;
  while (count < capacity) {
    const uint32_t indexInPeriod = count % periodSamples;
    const bool gateOn = indexInPeriod < onSamples;
    float amps = 0.0f;
    if (gateOn) {
      const float fraction =
          (onSamples > 1U) ? ((float)indexInPeriod / (float)(onSamples - 1U))
                           : 0.0f;
      amps = startAmps + ((endAmps - startAmps) * fraction);
    }
    out[count].timestampUs = timestamp;
    out[count].rawSample = ampsToRaw(amps);
    out[count].gateOn = gateOn ? 1U : 0U;
    out[count].clipped = 0U;
    timestamp += stepUs;
    count++;
  }
  return count;
}

void test_phase_analyze_rejects_bad_arguments_and_short_captures(void) {
  VP37CurrentPhaseSample samples[4] = {};
  VP37CurrentPhaseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        VP37_currentPhaseAnalyze(nullptr, 4U, &result));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        VP37_currentPhaseAnalyze(samples, 4U, nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        VP37_currentPhaseAnalyze(samples, 1U, &result));
  // All-off samples carry no gate edge.
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        VP37_currentPhaseAnalyze(samples, 4U, &result));
}

void test_phase_analyze_reports_discontinuous_conduction(void) {
  // Current collapses to zero every OFF phase: the coil mean really is the
  // chopped shunt mean.
  static VP37CurrentPhaseSample samples[1024];
  const uint32_t count =
      buildPhaseCapture(samples, 1024U, 0.0f, 5.0f, 56U, 164U, 30U);

  VP37CurrentPhaseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_currentPhaseAnalyze(samples, count, &result));
  TEST_ASSERT_TRUE(result.cycles >= 5U);
  TEST_ASSERT_UINT32_WITHIN(60U, 4920U, result.medianPeriodUs);
  TEST_ASSERT_UINT32_WITHIN(60U, 1650U, result.medianOnTimeUs);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, result.startAmps);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 5.0f, result.endAmps);
  TEST_ASSERT_FALSE(result.continuousConduction);
  TEST_ASSERT_FLOAT_WITHIN(0.3f, 3.03f, result.riseAmpsPerMs);
}

void test_phase_analyze_reports_continuous_conduction(void) {
  // The same duty, but the pulse starts at 3.4 A because the freewheel path
  // kept the coil conducting. The shunt mean is unchanged in shape yet the coil
  // carries far more current.
  static VP37CurrentPhaseSample samples[1024];
  const uint32_t count =
      buildPhaseCapture(samples, 1024U, 3.4f, 5.0f, 56U, 164U, 30U);

  VP37CurrentPhaseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_currentPhaseAnalyze(samples, count, &result));
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 3.4f, result.startAmps);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 5.0f, result.endAmps);
  TEST_ASSERT_TRUE(result.continuousConduction);
  // RMS over the whole cycle stays well below the ON-phase current because the
  // OFF phase contributes zero to the shunt.
  TEST_ASSERT_TRUE(result.rmsShuntAmps > 1.5f);
  TEST_ASSERT_TRUE(result.rmsShuntAmps < 4.3f);
  TEST_ASSERT_FLOAT_WITHIN(0.05f,
                           result.rmsShuntAmps * result.rmsShuntAmps *
                               VP37_CURRENT_SHUNT_OHMS,
                           result.shuntPowerWatts);
}

void test_phase_analyze_drops_cycles_with_a_changed_period(void) {
  static VP37CurrentPhaseSample samples[512];
  uint32_t count = buildPhaseCapture(samples, 512U, 3.4f, 5.0f, 56U, 164U, 30U);

  VP37CurrentPhaseResult reference;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_currentPhaseAnalyze(samples, count, &reference));

  // Stretch one period far past the tolerance by delaying the timestamps after
  // the second gate edge.
  for (uint32_t i = 200U; i < count; i++) {
    samples[i].timestampUs += 4000U;
  }

  VP37CurrentPhaseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_currentPhaseAnalyze(samples, count, &result));
  TEST_ASSERT_TRUE(result.rejectedCycles > 0U);
  TEST_ASSERT_TRUE(result.cycles < reference.cycles);
}

void test_phase_analyze_counts_clipped_samples(void) {
  static VP37CurrentPhaseSample samples[512];
  const uint32_t count =
      buildPhaseCapture(samples, 512U, 3.4f, 5.0f, 56U, 164U, 30U);
  for (uint32_t i = 0U; i < count; i++) {
    if (samples[i].gateOn != 0U) {
      samples[i].clipped = 1U;
    }
  }

  VP37CurrentPhaseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_currentPhaseAnalyze(samples, count, &result));
  TEST_ASSERT_TRUE(result.clippedSamples > 100U);
}

void test_phase_capture_times_out_without_a_gate_edge(void) {
  hal_mock_gpio_clear_read_sequence(PIO_VP37_RPM);
  hal_mock_gpio_inject_level(PIO_VP37_RPM, true); // gate idle, MOSFET off

  VP37CurrentPhaseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_ETIMEOUT,
                        VP37_currentSensePhaseCapture(nullptr, &result));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        VP37_currentSensePhaseCapture(nullptr, nullptr));

  uint32_t stored = 1U;
  TEST_ASSERT_NULL(VP37_currentPhaseSamples(&stored));
  TEST_ASSERT_EQUAL_UINT32(0U, stored);
  TEST_ASSERT_NULL(VP37_currentPhaseSamples(nullptr));
}

void test_phase_capture_aligns_on_the_gate_turn_on_edge(void) {
  // The first scripted read seeds the previous level, the second is the edge.
  const bool levels[2] = {true, false};
  hal_mock_gpio_push_read_sequence(PIO_VP37_RPM, levels, 2U);
  hal_mock_gpio_inject_level(PIO_VP37_RPM, false); // stays on afterwards
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 1365);

  // The capture aligns and fills the buffer; a level that never toggles again
  // yields no complete cycle, which the analysis reports rather than inventing.
  VP37CurrentPhaseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        VP37_currentSensePhaseCapture(nullptr, &result));

  uint32_t stored = 0U;
  const VP37CurrentPhaseSample *buffer = VP37_currentPhaseSamples(&stored);
  TEST_ASSERT_NOT_NULL(buffer);
  TEST_ASSERT_EQUAL_UINT32(VP37_CURRENT_PHASE_SAMPLES, stored);
  TEST_ASSERT_EQUAL_UINT8(1U, buffer[0].gateOn);
  TEST_ASSERT_TRUE(buffer[1].timestampUs > buffer[0].timestampUs);
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
  RUN_TEST(test_histogram_percentile_rejects_bad_arguments);
  RUN_TEST(test_histogram_percentile_walks_from_the_requested_bin);
  RUN_TEST(test_expected_active_run_follows_the_commanded_duty);
  RUN_TEST(test_percentiles_describe_the_on_phase_shape);
  RUN_TEST(test_clipped_samples_are_counted);
  RUN_TEST(test_shunt_rms_and_power_use_the_mean_square);
  RUN_TEST(test_run_plausibility_rejects_a_fragmented_window);
  RUN_TEST(test_phase_analyze_rejects_bad_arguments_and_short_captures);
  RUN_TEST(test_phase_analyze_reports_discontinuous_conduction);
  RUN_TEST(test_phase_analyze_reports_continuous_conduction);
  RUN_TEST(test_phase_analyze_drops_cycles_with_a_changed_period);
  RUN_TEST(test_phase_analyze_counts_clipped_samples);
  RUN_TEST(test_phase_capture_times_out_without_a_gate_edge);
  RUN_TEST(test_phase_capture_aligns_on_the_gate_turn_on_edge);
  return UNITY_END();
}
