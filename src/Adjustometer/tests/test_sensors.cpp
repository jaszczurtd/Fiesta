/**
 * @file test_sensors.cpp
 * @brief Host-side tests for Adjustometer sensor logic (capture, baseline,
 *        thermal compensation, zero-hold, status bitmask).
 */

#include "hal/impl/.mock/hal_mock.h"
#include "include/adj_capture_fixture.h"
#include "sensors.h"
#include "utils/unity.h"

/* ── Helpers ─────────────────────────────────────────────────────────────────
 */

/**
 * Lock baseline at freqHz by driving enough pulses to exceed
 * ADJUSTOMETER_BASELINE_MAX_TIME_MS (force convergence) plus
 * ADJUSTOMETER_BASELINE_VERIFY_MS (post-convergence verification).
 * Pulse window is 128 (internal constant in sensors.c).
 */
static void lockBaseline(uint32_t freqHz) {
  const uint32_t pulseWindow = 128U;
  const uint32_t periodUs = 1000000U / freqHz;
  const uint32_t windowUs = pulseWindow * periodUs;
  const uint32_t totalTimeUs =
      (ADJUSTOMETER_BASELINE_MAX_TIME_MS + ADJUSTOMETER_BASELINE_VERIFY_MS) *
      1000UL;
  const uint32_t windows = (totalTimeUs / windowUs) + 5U;
  adj_test_capture_pulses(windows * pulseWindow + 1U, freqHz);
}

/**
 * Inject ADC for supply voltage at given tenths-of-volt.
 * Accounts for hal_adc_compensate_rp2040_12bit offset (+8..+32) by
 * back-calculating.
 */
static void injectVoltage(int tenthsOfVolt) {
  const float ratio =
      (float)(VDIV_R1_KOHM + VDIV_R2_KOHM) / (float)VDIV_R2_KOHM;
  float volts = (float)tenthsOfVolt / 10.0f;
  int adc = (int)(volts / ratio * 4095.0f / 3.3f);
  if (adc < 0)
    adc = 0;
  hal_mock_adc_inject(ADC_VOLT_PIN, adc);
}

/**
 * Inject ADC for fuel temperature sensor.
 * broken=true -> ADC near max -> NTC conversion fails -> 0.
 * broken=false -> ADC mid-range -> NTC conversion returns a positive value.
 */
static void injectFuelTemp(bool broken) {
  hal_mock_adc_inject(ADC_FUEL_TEMP_PIN, broken ? 4090 : 2000);
}

/**
 * Call getFuelTemperatureRaw / getSupplyVoltageRaw multiple times
 * so the EMA filter settles to the injected value.
 */
static void settleAdcFilters(void) {
  for (int i = 0; i < 30; i++) {
    updateAuxiliarySensors();
  }
}

/* ── Setup / Teardown ────────────────────────────────────────────────────────
 */

void setUp(void) {
  hal_mock_set_micros(1);
  hal_mock_set_millis(0);
  injectVoltage(120);
  injectFuelTemp(false);
  initSensors();
}

void tearDown(void) {}

/* ── Frequency computation ───────────────────────────────────────────────────
 */

void test_no_pulses_returns_zero(void) {
  TEST_ASSERT_EQUAL_INT32(0, getAdjustometerPulses());
}

void test_pulses_before_baseline_returns_zero(void) {
  adj_test_capture_pulses(256, 10000);
  TEST_ASSERT_EQUAL_INT32(0, getAdjustometerPulses());
}

/* ── Baseline lock ───────────────────────────────────────────────────────────
 */

void test_baseline_locks_after_stable_signal(void) {
  lockBaseline(10000);
  adj_test_capture_pulses(256, 10000);
  int32_t pulse = getAdjustometerPulses();
  TEST_ASSERT_INT32_WITHIN(15, 0, pulse);
}

void test_frequency_shift_produces_nonzero_pulse(void) {
  lockBaseline(10000);
  /* Shift from 10000 Hz baseline to ~11111 Hz (period 90us) */
  adj_test_capture_pulses(2048, 11111);
  int32_t pulse = getAdjustometerPulses();
  TEST_ASSERT_TRUE(pulse > 200);
  TEST_ASSERT_TRUE(getAdjustometerSignalHz() > getBaseline());
  TEST_ASSERT_TRUE(getAdjustometerSignedDeltaHz() > 200);
}

void test_signed_delta_preserves_negative_frequency_shift(void) {
  lockBaseline(10000);
  /* Shift below baseline to ~9009 Hz (period 111 us). */
  adj_test_capture_pulses(2048, 9009);

  TEST_ASSERT_TRUE(getAdjustometerPulses() > 200);
  TEST_ASSERT_TRUE(getAdjustometerSignalHz() < getBaseline());
  TEST_ASSERT_TRUE(getAdjustometerSignedDeltaHz() < -200);
}

/* ── Zero-hold hysteresis ────────────────────────────────────────────────────
 */

void test_small_shift_within_zero_hold(void) {
  /* Capture a real small shift with sub-microsecond tick precision. */
  lockBaseline(10000);
  adj_test_capture_pulses(512, 10010);
  TEST_ASSERT_TRUE(isAdjustometerReady());
  TEST_ASSERT_BITS_LOW(ADJ_STATUS_SIGNAL_LOST, getAdjustometerStatus());
  TEST_ASSERT_GREATER_THAN_UINT32(10000U, getAdjustometerSignalHz());
  int32_t pulse = getAdjustometerPulses();
  TEST_ASSERT_EQUAL_INT32(0, pulse);
}

/* ── Signal loss ─────────────────────────────────────────────────────────────
 */

void test_signal_lost_returns_zero(void) {
  lockBaseline(10000);
  adj_test_capture_pulses(256, 10000);
  /* Advance time well past signal loss timeout without any pulses */
  hal_mock_advance_micros(ADJUSTOMETER_SIGNAL_LOSS_MAX_US + 100000U);
  TEST_ASSERT_EQUAL_INT32(0, getAdjustometerPulses());
}

/* ── Status bitmask ──────────────────────────────────────────────────────────
 */

void test_status_signal_lost_when_no_pulses(void) {
  hal_mock_advance_micros(300000);
  uint8_t status = getAdjustometerStatus();
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_SIGNAL_LOST, status);
}

void test_status_baseline_pending_initially(void) {
  uint8_t status = getAdjustometerStatus();
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_BASELINE_PENDING, status);
}

void test_status_baseline_clears_after_lock(void) {
  lockBaseline(10000);
  adj_test_capture_pulses(128, 10000);
  uint8_t status = getAdjustometerStatus();
  TEST_ASSERT_BITS_LOW(ADJ_STATUS_BASELINE_PENDING, status);
}

void test_status_fuel_temp_broken(void) {
  injectFuelTemp(true);
  settleAdcFilters();
  uint8_t status = getAdjustometerStatus();
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_FUEL_TEMP_BROKEN, status);
}

void test_status_fuel_temp_ok(void) {
  injectFuelTemp(false);
  settleAdcFilters();
  uint8_t status = getAdjustometerStatus();
  TEST_ASSERT_BITS_LOW(ADJ_STATUS_FUEL_TEMP_BROKEN, status);
}

void test_status_voltage_too_low(void) {
  injectVoltage(50); /* 5.0 V - well below 8.0 V threshold */
  settleAdcFilters();
  uint8_t status = getAdjustometerStatus();
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_VOLTAGE_BAD, status);
}

void test_status_voltage_too_high(void) {
  injectVoltage(200); /* 20.0 V - well above 15.0 V threshold */
  settleAdcFilters();
  uint8_t status = getAdjustometerStatus();
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_VOLTAGE_BAD, status);
}

void test_status_voltage_ok(void) {
  injectVoltage(120); /* 12.0 V */
  settleAdcFilters();
  uint8_t status = getAdjustometerStatus();
  TEST_ASSERT_BITS_LOW(ADJ_STATUS_VOLTAGE_BAD, status);
}

void test_status_multiple_bits(void) {
  injectFuelTemp(true);
  injectVoltage(50);
  settleAdcFilters();
  uint8_t status = getAdjustometerStatus();
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_FUEL_TEMP_BROKEN, status);
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_VOLTAGE_BAD, status);
}

/* ── Thermal compensation guard ──────────────────────────────────────────────
 */

void test_thermal_comp_skipped_when_sensor_broken(void) {
  injectFuelTemp(false);
  settleAdcFilters();
  lockBaseline(10000);
  adj_test_capture_pulses(256, 10000);
  int32_t pulseOk = getAdjustometerPulses();

  /* settleAdcFilters() advances mock micros (~2400 us via hal_delay_us),
   * creating a timing gap that corrupts the first capture window.
   * Feed enough extra pulses (16 windows) to flush the gap and let
   * the EMA reconverge to the true frequency. */
  injectFuelTemp(true);
  settleAdcFilters();
  adj_test_capture_pulses(4096 + 256, 10000);
  int32_t pulseBroken = getAdjustometerPulses();

  TEST_ASSERT_INT32_WITHIN(15, 0, pulseOk);
  TEST_ASSERT_INT32_WITHIN(15, 0, pulseBroken);
}

/* ── NaN immunity (regression for hal_adc_compensate_rp2040_12bit > ADC_MAX)
 * ────────────────────────
 */

/**
 * When ADC reads near max (4090), hal_adc_compensate_rp2040_12bit pushes it to
 * ~4122 (> ADC_MAX=4095). The historical NTC calculation evaluated 4095/4122-1
 * and then log(negative), producing NaN. Without the isnan guard, NaN
 * permanently poisons the EMA filter. This test verifies the filter recovers
 * after a broken sensor phase.
 */
void test_nan_recovery_after_broken_sensor(void) {
  injectFuelTemp(true); /* ADC=4090 -> compensated value exceeds 4095. */
  settleAdcFilters();
  uint8_t broken = getFuelTemperatureRaw();
  TEST_ASSERT_EQUAL_UINT8(ADJ_FUEL_TEMP_SENSOR_BROKEN, broken);

  injectFuelTemp(false); /* ADC=2000 -> approximately 13 degrees C. */
  for (int i = 0; i < 30; i++)
    getFuelTemperatureRaw();
  uint8_t recovered = getFuelTemperatureRaw();
  TEST_ASSERT_TRUE(recovered > 0); /* Must escape NaN trap */
}

/* ── getAdjustometerStatus must not mutate temperature EMA ───────────────────
 */

/**
 * Calling getAdjustometerStatus() repeatedly should not change the
 * fuel temperature reading - it reads adjustometerSharedFuelTemp
 * atomically instead of calling getFuelTemperatureRaw().
 *
 * We inject a moderate ADC change (not broken sensor) so that
 * one EMA step produces a small shift, while 50 hidden EMA steps
 * (the old side-effect) would produce a large drift.
 */
void test_status_does_not_mutate_fuel_temp_ema(void) {
  injectFuelTemp(false); /* ADC=2000 -> ~13°C */
  settleAdcFilters();
  uint8_t tempBefore = getFuelTemperatureRaw();
  TEST_ASSERT_TRUE(tempBefore > 0);

  /* Shift ADC to a significantly different (but valid) value */
  hal_mock_adc_inject(ADC_FUEL_TEMP_PIN, 1000);
  /* 50 status calls - must not advance the temperature EMA */
  for (int i = 0; i < 50; i++)
    getAdjustometerStatus();

  /* First getFuelTemperatureRaw after 50 status calls:
   * only 1 EMA step should happen, so the shift must be small. */
  uint8_t tempAfter = getFuelTemperatureRaw();
  /* With the old side-effect bug: tempAfter would be far from tempBefore
   * (50 EMA steps toward the new ADC value). Fixed: only 1 step. */
  TEST_ASSERT_INT_WITHIN(5, tempBefore, tempAfter);
}

/* ── Signal-loss consistency ─────────────────────────────────────────────────
 */

/**
 * getAdjustometerPulses() and getAdjustometerStatus() must agree on
 * signal-loss: if pulses returns 0 due to signal loss, status must
 * have SIGNAL_LOST bit set, and vice versa.
 */
void test_signal_loss_consistency_pulses_vs_status(void) {
  lockBaseline(10000);
  adj_test_capture_pulses(256, 10000);

  /* Signal alive - both should agree */
  TEST_ASSERT_TRUE(getAdjustometerPulses() >= 0);
  TEST_ASSERT_BITS_LOW(ADJ_STATUS_SIGNAL_LOST, getAdjustometerStatus());

  /* Signal lost */
  hal_mock_advance_micros(ADJUSTOMETER_SIGNAL_LOSS_MAX_US + 100000U);
  int32_t pulses = getAdjustometerPulses();
  uint8_t status = getAdjustometerStatus();
  TEST_ASSERT_EQUAL_INT32(0, pulses);
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_SIGNAL_LOST, status);
}

/* ── Runner ─────────────────────────────────────────────────────────────────
 */

void test_feedback_exposes_raw_window_and_status_does_not_sample_adc(void) {
  lockBaseline(10000);
  updateAuxiliarySensors();
  // ADC advances mock time without generating edges; finish that window first.
  adj_test_capture_pulses(128, 10000);
  adjustometer_feedback_t before, after;
  TEST_ASSERT_EQUAL_INT(HAL_OK, getAdjustometerFeedback(&before));
  adj_test_capture_pulses(128, 8000);
  TEST_ASSERT_EQUAL_INT(HAL_OK, getAdjustometerFeedback(&after));
  TEST_ASSERT_EQUAL_UINT32(before.number + 1U, after.number);
  TEST_ASSERT_EQUAL_UINT32(8000U, after.rawHz);
  TEST_ASSERT_GREATER_THAN_UINT32(after.rawHz, after.filteredHz);
  TEST_ASSERT_EQUAL_UINT32(before.baselineHz, after.baselineHz);
  const uint32_t now = hal_micros();
  for (int i = 0; i < 10; i++) {
    (void)getAdjustometerStatus();
  }
  TEST_ASSERT_EQUAL_UINT32(now, hal_micros());
  hal_mock_advance_micros(70000);
  TEST_ASSERT_EQUAL_INT(HAL_OK, getAdjustometerFeedback(&after));
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, after.ageUs);
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_SIGNAL_LOST, after.status);
}

void test_capture_delayed_drain_preserves_hardware_frequency(void) {
  uint32_t ticks = hal_micros() * 16U;
  for (unsigned i = 0; i < 129U; ++i) {
    ticks += 448U;
    hal_mock_advance_micros(28U);
    TEST_ASSERT_EQUAL(HAL_OK, hal_mock_pulse_capture_edge(ticks, hal_micros()));
  }
  const uint32_t measured = hal_micros();
  hal_mock_advance_micros(2000U);
  updateAdjustometerCapture();
  adjustometer_feedback_t feedback;
  TEST_ASSERT_EQUAL(HAL_OK, getAdjustometerFeedback(&feedback));
  TEST_ASSERT_EQUAL_UINT32(35714U, feedback.rawHz);
  TEST_ASSERT_EQUAL_UINT32(measured, feedback.measuredUs);
  TEST_ASSERT_BITS_LOW(ADJ_STATUS_SIGNAL_LOST, feedback.status);
}

void test_capture_overflow_invalidates_and_recovers_without_rezero(void) {
  lockBaseline(10000);
  const uint32_t baseline = getBaseline();
  hal_mock_pulse_capture_fault(HAL_EOVERFLOW);
  updateAdjustometerCapture();
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_SIGNAL_LOST, getAdjustometerStatus());
  TEST_ASSERT_EQUAL_INT32(0, getAdjustometerPulses());
  hal_mock_advance_micros(99999U);
  updateAdjustometerCapture();
  TEST_ASSERT_EQUAL(HAL_EUNINIT, hal_mock_pulse_capture_edge(0, hal_micros()));
  hal_mock_advance_micros(1U);
  updateAdjustometerCapture();
  adj_test_capture_pulses(128U, 10000U);
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_SIGNAL_LOST, getAdjustometerStatus());
  adj_test_capture_pulses(1U, 10000U);
  TEST_ASSERT_BITS_LOW(ADJ_STATUS_SIGNAL_LOST, getAdjustometerStatus());
  TEST_ASSERT_TRUE(isAdjustometerReady());
  TEST_ASSERT_EQUAL_UINT32(baseline, getBaseline());
}

void test_capture_loss_restarts_pending_baseline_verification(void) {
  adj_test_capture_pulses(4097U, 10000U);
  TEST_ASSERT_FALSE(isAdjustometerReady());
  hal_mock_advance_micros(1500000U);
  updateAdjustometerCapture();
  adj_test_capture_pulses(256U, 10000U);
  TEST_ASSERT_FALSE(isAdjustometerReady());
  lockBaseline(10000U);
  TEST_ASSERT_TRUE(isAdjustometerReady());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_capture_loss_restarts_pending_baseline_verification);
  RUN_TEST(test_capture_delayed_drain_preserves_hardware_frequency);
  RUN_TEST(test_capture_overflow_invalidates_and_recovers_without_rezero);
  RUN_TEST(test_feedback_exposes_raw_window_and_status_does_not_sample_adc);
  RUN_TEST(test_no_pulses_returns_zero);
  RUN_TEST(test_pulses_before_baseline_returns_zero);
  RUN_TEST(test_baseline_locks_after_stable_signal);
  RUN_TEST(test_frequency_shift_produces_nonzero_pulse);
  RUN_TEST(test_signed_delta_preserves_negative_frequency_shift);
  RUN_TEST(test_small_shift_within_zero_hold);
  RUN_TEST(test_signal_lost_returns_zero);
  RUN_TEST(test_status_signal_lost_when_no_pulses);
  RUN_TEST(test_status_baseline_pending_initially);
  RUN_TEST(test_status_baseline_clears_after_lock);
  RUN_TEST(test_status_fuel_temp_broken);
  RUN_TEST(test_status_fuel_temp_ok);
  RUN_TEST(test_status_voltage_too_low);
  RUN_TEST(test_status_voltage_too_high);
  RUN_TEST(test_status_voltage_ok);
  RUN_TEST(test_status_multiple_bits);
  RUN_TEST(test_thermal_comp_skipped_when_sensor_broken);
  RUN_TEST(test_nan_recovery_after_broken_sensor);
  RUN_TEST(test_status_does_not_mutate_fuel_temp_ema);
  RUN_TEST(test_signal_loss_consistency_pulses_vs_status);
  return UNITY_END();
}
