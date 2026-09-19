#include "dtcManager.h"
#include "hal/impl/.mock/hal_mock.h"
#include "rpm.h"
#include "sensors.h"
#include "testable/sensors_testable.h"
#include "vp37_current.h"

#include "unity.h"
#include <hal/analog/hal_adc_scan.h>
#include <math.h>

/*
 * sensors.cpp calculation tests - functions that operate purely on global
 * value fields or on injected ADC values (no hardware timing required).
 *
 * Relevant constants (config.h / hardwareConfig.h):
 *   PWM_RESOLUTION   = 2047
 *   THROTTLE_MIN     = 1795   ADC count at full throttle (inverted)
 *   THROTTLE_MAX     = 3730   ADC count at idle
 *   FUEL_MAX         = 320
 *   FUEL_MIN         = 1280
 *   RPM_MAX_EVER     = 5000
 *   ADC_SENSORS_PIN  = 27
 *
 * readThrottle() maps ADC reading to [0, PWM_RESOLUTION] and then inverts:
 *   return abs(result - PWM_RESOLUTION)
 *   -> low ADC (= THROTTLE_MIN) -> full throttle value = PWM_RESOLUTION
 *   -> high ADC (= THROTTLE_MAX) -> idle = 0
 *
 * getPercentageEngineLoad():
 *   map = F_PRESSURE * 255 / 2.55
 *   load = (map/255) * (F_RPM / RPM_MAX_EVER) * 100
 */

void setUp(void) {
  initSensors();
  hal_mock_set_millis(0);
  hal_mock_adc_inject(ADC_SENSORS_PIN, 0);
}

void tearDown(void) {}

// ── getThrottlePercentage
// ─────────────────────────────────────────────────────

void test_throttle_percentage_zero(void) {
  setGlobalValue(F_THROTTLE_POS, 0.0f);
  TEST_ASSERT_EQUAL_INT(0, getThrottlePercentage());
}

void test_throttle_percentage_full(void) {
  setGlobalValue(F_THROTTLE_POS, (float)PWM_RESOLUTION);
  TEST_ASSERT_EQUAL_INT(100, getThrottlePercentage());
}

void test_throttle_percentage_midpoint(void) {
  /* PWM_RESOLUTION / 2 ≈ 1023 -> (1023*100)/2047 ≈ 49 % */
  setGlobalValue(F_THROTTLE_POS, (float)(PWM_RESOLUTION / 2));
  int pct = getThrottlePercentage();
  TEST_ASSERT_INT_WITHIN(2, 50, pct);
}

void test_throttle_percentage_quarter(void) {
  setGlobalValue(F_THROTTLE_POS, (float)(PWM_RESOLUTION / 4));
  int pct = getThrottlePercentage();
  TEST_ASSERT_INT_WITHIN(2, 25, pct);
}

void test_throttle_legacy_percentage_retains_integer_conversion(void) {
  setGlobalValue(F_THROTTLE_POS, 20.9f);
  TEST_ASSERT_EQUAL_INT(0, getThrottlePercentage());
  setGlobalValue(F_THROTTLE_POS, 21.9f);
  TEST_ASSERT_EQUAL_INT(1, getThrottlePercentage());
}

static float sampleDriverDemand(int adc, uint32_t ms) {
  hal_mock_set_millis(ms);
  hal_mock_adc_inject(ADC_SENSORS_PIN, adc);
  readThrottleValues();
  return getDriverDemandPercent();
}

void test_driver_demand_first_sample_retains_fractional_resolution(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getDriverDemandPercent());
  const float demand = sampleDriverDemand(2700, 0U);
  const float rawPercent =
      getGlobalValue(F_THROTTLE_POS) / (float)PWM_RESOLUTION * 100.0f;
  TEST_ASSERT_FLOAT_WITHIN(.00001f, rawPercent, demand);
  TEST_ASSERT_GREATER_THAN_FLOAT(.01f, fabsf(demand - roundf(demand)));
}

void test_driver_demand_filters_steps_without_delaying_raw_cache(void) {
  const float initial = sampleDriverDemand(3200, 0U);
  float previous = sampleDriverDemand(2400, 10U);
  const float rawPercent =
      getGlobalValue(F_THROTTLE_POS) / (float)PWM_RESOLUTION * 100.0f;
  TEST_ASSERT_GREATER_THAN_FLOAT(initial, previous);
  TEST_ASSERT_LESS_THAN_FLOAT(rawPercent, previous);
  TEST_ASSERT_INT_WITHIN(1, (int)rawPercent, getThrottlePercentage());
  for (uint32_t ms = 20U; ms <= 500U; ms += 10U) {
    const float demand = sampleDriverDemand(2400, ms);
    TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(previous, demand);
    TEST_ASSERT_LESS_OR_EQUAL_FLOAT(rawPercent, demand);
    previous = demand;
  }
  TEST_ASSERT_FLOAT_WITHIN(.11f, rawPercent, previous);
}

void test_driver_demand_rejects_sub_deadband_adc_jitter(void) {
  const float initial = sampleDriverDemand(2700, 0U);
  for (uint32_t ms = 10U; ms <= 500U; ms += 10U) {
    const int adc = (ms % 20U) == 0U ? 2699 : 2701;
    TEST_ASSERT_EQUAL_FLOAT(initial, sampleDriverDemand(adc, ms));
  }
}

void test_driver_demand_reaches_endpoints_and_releases_zero_or_failed_input(
    void) {
  TEST_ASSERT_EQUAL_FLOAT(100.0f, sampleDriverDemand(0, 0U));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sampleDriverDemand(4095, 10U));
  const float rising = sampleDriverDemand(0, 20U);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, rising);
  TEST_ASSERT_LESS_THAN_FLOAT(100.0f, rising);
  for (uint32_t ms = 30U; ms <= 400U; ms += 10U) {
    const float demand = sampleDriverDemand(0, ms);
    TEST_ASSERT_TRUE(demand >= 0.0f && demand <= 100.0f);
  }
  TEST_ASSERT_EQUAL_FLOAT(100.0f, getDriverDemandPercent());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sampleDriverDemand(THROTTLE_MAX, 410U));
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, sampleDriverDemand(0, 420U));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, sampleDriverDemand(-1, 430U));
}

void test_driver_demand_uses_elapsed_sample_time_across_clock_wrap(void) {
  const float initial = sampleDriverDemand(3200, UINT32_MAX - 9U);
  const float demand = sampleDriverDemand(2400, 5U);
  const float rawPercent =
      getGlobalValue(F_THROTTLE_POS) / (float)PWM_RESOLUTION * 100.0f;
  // Fifteen elapsed milliseconds moves one third toward the new sample.
  TEST_ASSERT_FLOAT_WITHIN(.0001f, initial + (rawPercent - initial) / 3.0f,
                           demand);
  TEST_ASSERT_EQUAL_FLOAT(demand, sampleDriverDemand(2400, 5U));
}

void test_driver_demand_reset_and_getter_do_not_reuse_or_advance_filter(void) {
  const float initial = sampleDriverDemand(2700, 0U);
  setGlobalValue(F_THROTTLE_POS, NAN);
  hal_mock_adc_inject(ADC_SENSORS_PIN, 0);
  for (uint32_t ms = 10U; ms <= 500U; ms += 10U) {
    hal_mock_set_millis(ms);
    TEST_ASSERT_EQUAL_FLOAT(initial, getDriverDemandPercent());
  }
  initSensors();
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getDriverDemandPercent());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getGlobalValue(F_THROTTLE_POS));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, sampleDriverDemand(0, 510U));
}

// ── getPercentageEngineLoad
// ───────────────────────────────────────────────────

void test_engine_load_zero_when_rpm_zero(void) {
  setGlobalValue(F_RPM, 0.0f);
  setGlobalValue(F_PRESSURE, 1.0f);
  TEST_ASSERT_EQUAL_INT(0, getPercentageEngineLoad());
}

void test_engine_load_zero_when_pressure_zero(void) {
  setGlobalValue(F_RPM, (float)RPM_MAX_EVER);
  setGlobalValue(F_PRESSURE, 0.0f);
  TEST_ASSERT_EQUAL_INT(0, getPercentageEngineLoad());
}

void test_engine_load_full_at_max_rpm_and_pressure(void) {
  /* map = 2.55 * 255 / 2.55 = 255; load = (255/255)*(5000/5000)*100 = 100 */
  setGlobalValue(F_RPM, (float)RPM_MAX_EVER);
  setGlobalValue(F_PRESSURE, 2.55f);
  TEST_ASSERT_EQUAL_INT(100, getPercentageEngineLoad());
}

void test_engine_load_half_at_half_rpm(void) {
  /* map = 255, rpm = 2500/5000 = 0.5 -> load = 50 */
  setGlobalValue(F_RPM, (float)(RPM_MAX_EVER / 2));
  setGlobalValue(F_PRESSURE, 2.55f);
  TEST_ASSERT_INT_WITHIN(2, 50, getPercentageEngineLoad());
}

void test_engine_load_half_at_half_pressure(void) {
  /* map = 127.5/255 = 0.5, rpm full -> load = 50 */
  setGlobalValue(F_RPM, (float)RPM_MAX_EVER);
  setGlobalValue(F_PRESSURE, 1.275f);
  TEST_ASSERT_INT_WITHIN(2, 50, getPercentageEngineLoad());
}

void test_engine_load_clamped_to_100(void) {
  /* Values beyond spec should be clamped to 100 */
  setGlobalValue(F_RPM, (float)(RPM_MAX_EVER * 2));
  setGlobalValue(F_PRESSURE, 5.0f);
  TEST_ASSERT_EQUAL_INT(100, getPercentageEngineLoad());
}

void test_engine_load_never_negative(void) {
  /* Negative pressure should yield load = 0 */
  setGlobalValue(F_RPM, (float)RPM_MAX_EVER);
  setGlobalValue(F_PRESSURE, -1.0f);
  TEST_ASSERT_EQUAL_INT(0, getPercentageEngineLoad());
}

// ── isDPFRegenerating
// ─────────────────────────────────────────────────────────

void test_dpf_not_regenerating_by_default(void) {
  TEST_ASSERT_FALSE(isDPFRegenerating());
}

void test_dpf_regenerating_when_flag_set(void) {
  setGlobalValue(F_DPF_REGEN, 1.0f);
  TEST_ASSERT_TRUE(isDPFRegenerating());
}

void test_dpf_not_regenerating_after_flag_clear(void) {
  setGlobalValue(F_DPF_REGEN, 1.0f);
  TEST_ASSERT_TRUE(isDPFRegenerating());
  setGlobalValue(F_DPF_REGEN, 0.0f);
  TEST_ASSERT_FALSE(isDPFRegenerating());
}

// ── global value index guards
// ──────────────────────────────────────────────────

void test_get_global_value_invalid_index_returns_zero(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, getGlobalValue(-1));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, getGlobalValue(F_LAST));
}

void test_set_global_value_invalid_index_does_not_modify_valid_slot(void) {
  setGlobalValue(F_RPM, 321.0f);
  setGlobalValue(-1, 999.0f);
  setGlobalValue(F_LAST, 999.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 321.0f, getGlobalValue(F_RPM));
}

void test_init_spi_does_not_drive_vp37_current_sense_pin(void) {
  hal_gpio_set_mode(ADC_VP37_CURRENT_PIN, HAL_GPIO_INPUT);
  initSPI();
  TEST_ASSERT_FALSE(hal_mock_gpio_is_output(ADC_VP37_CURRENT_PIN));
}

// ── internal testable helpers ───────────────────────────────────────────────

void test_internal_throttle_helper_maps_min_to_full_scale(void) {
  TEST_ASSERT_EQUAL_INT(PWM_RESOLUTION,
                        sensors_computeThrottlePositionFromRaw(THROTTLE_MIN));
}

void test_internal_throttle_helper_maps_max_to_zero(void) {
  TEST_ASSERT_EQUAL_INT(0,
                        sensors_computeThrottlePositionFromRaw(THROTTLE_MAX));
}

void test_internal_throttle_helper_clamps_below_min(void) {
  TEST_ASSERT_EQUAL_INT(PWM_RESOLUTION, sensors_computeThrottlePositionFromRaw(
                                            THROTTLE_MIN - 200));
}

void test_internal_throttle_helper_clamps_above_max(void) {
  TEST_ASSERT_EQUAL_INT(
      0, sensors_computeThrottlePositionFromRaw(THROTTLE_MAX + 200));
}

void test_internal_engine_load_helper_rounds_half_up(void) {
  TEST_ASSERT_EQUAL_INT(67,
                        sensors_calculateEngineLoadFromValues(2.55f, 3325.0f));
}

void test_internal_engine_load_helper_clamps_negative_to_zero(void) {
  TEST_ASSERT_EQUAL_INT(
      0, sensors_calculateEngineLoadFromValues(-1.0f, (float)RPM_MAX_EVER));
}

void test_internal_engine_load_helper_clamps_overflow_to_hundred(void) {
  TEST_ASSERT_EQUAL_INT(100, sensors_calculateEngineLoadFromValues(
                                 5.0f, (float)(RPM_MAX_EVER * 2)));
}

// ── PCF8574 I2C write path (exercises the shared i2c-write helper) ──────────

void test_pcf8574_write_targets_expected_address(void) {
  hal_mock_i2c_set_busy(false);
  pcf8574_write(0, true);
  TEST_ASSERT_EQUAL_UINT8(PCF8574_ADDR, hal_mock_i2c_get_last_addr());
}

void test_pcf8574_write_changes_address_free_after_release(void) {
  // Each pcf8574_write must acquire and release the i2c bus mutex. The mock
  // exposes a lock depth counter that should be zero outside the helper.
  hal_mock_i2c_set_busy(false);
  pcf8574_write(1, true);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth());
  pcf8574_write(1, false);
  TEST_ASSERT_EQUAL_INT(0, hal_mock_i2c_get_lock_depth());
}

void test_pcf8574_write_invalid_pin_is_noop(void) {
  hal_mock_i2c_set_busy(false);
  // Use an address-distinct target first, then verify invalid pin leaves the
  // recorded last-address unchanged because pcf8574_write should not transmit.
  hal_i2c_begin_transmission(0x11);
  (void)hal_i2c_end_transmission();
  TEST_ASSERT_EQUAL_UINT8(0x11, hal_mock_i2c_get_last_addr());

  pcf8574_write(8, true); // pin > 7 -> invalid, early return
  TEST_ASSERT_EQUAL_UINT8(0x11, hal_mock_i2c_get_last_addr());
}

void test_pcf8574_read_returns_set_bit(void) {
  // PCF8574 reports the full port byte; bit 3 set -> pcf8574_read(3) == true.
  hal_mock_i2c_set_busy(false);
  const uint8_t rx[] = {(uint8_t)(1u << 3)};
  hal_mock_i2c_inject_rx(rx, 1);

  TEST_ASSERT_TRUE(pcf8574_read(3));
}

void test_pcf8574_read_returns_cleared_bit(void) {
  hal_mock_i2c_set_busy(false);
  const uint8_t rx[] = {0x00};
  hal_mock_i2c_inject_rx(rx, 1);

  TEST_ASSERT_FALSE(pcf8574_read(5));
}

void test_pcf8574_read_invalid_pin_returns_false(void) {
  // Invalid pin must short-circuit without touching the bus.
  hal_mock_i2c_set_busy(false);
  TEST_ASSERT_FALSE(pcf8574_read(8));
}

// ── readHighValues field wiring ─────────────────────────────────────────────
//
// readHighValues() is the core polling step on the sensor timer. It must
// refresh F_RPM, F_PRESSURE, F_GPS_CAR_SPEED and
// F_CALCULATED_ENGINE_LOAD on every tick, and must NOT touch fields owned by
// readMediumValues() (F_COOLANT_TEMP, F_OIL_TEMP, F_INTAKE_TEMP, F_FUEL,
// F_VOLTS). These tests guard the explicit setGlobalValue() wiring against
// accidental removal after the reflectionValueFields cleanup.

void test_throttle_poll_updates_only_its_cache_and_getters_do_not_resample(
    void) {
  for (int i = 0; i < F_LAST; ++i) {
    setGlobalValue(i, 1000.0f + (float)i);
  }
  hal_mock_adc_inject(ADC_SENSORS_PIN, 0);
  readThrottleValues();
  TEST_ASSERT_EQUAL_FLOAT((float)PWM_RESOLUTION,
                          getGlobalValue(F_THROTTLE_POS));
  for (int i = 0; i < F_LAST; ++i) {
    if (i != F_THROTTLE_POS) {
      TEST_ASSERT_EQUAL_FLOAT(1000.0f + (float)i, getGlobalValue(i));
    }
  }

  hal_mock_adc_inject(ADC_SENSORS_PIN, THROTTLE_MAX);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, 100.0f, getDriverDemandPercent());
  TEST_ASSERT_EQUAL_INT(100, getThrottlePercentage());
  readThrottleValues();
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getGlobalValue(F_THROTTLE_POS));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getDriverDemandPercent());

  setGlobalValue(F_THROTTLE_POS, 1000.0f);
  hal_mock_adc_inject(ADC_SENSORS_PIN, -1);
  readThrottleValues();
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getGlobalValue(F_THROTTLE_POS));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getDriverDemandPercent());
}

void test_readHighValues_preserves_the_independently_sampled_throttle(void) {
  const float demand = sampleDriverDemand(2700, 0U);
  setGlobalValue(F_THROTTLE_POS, 1234.0f);
  hal_mock_adc_inject(ADC_SENSORS_PIN, THROTTLE_MAX);
  readHighValues();
  TEST_ASSERT_EQUAL_FLOAT(1234.0f, getGlobalValue(F_THROTTLE_POS));
  TEST_ASSERT_EQUAL_FLOAT(demand, getDriverDemandPercent());
}

void test_readHighValues_refreshes_rpm_from_instance(void) {
  getRPMInstance()->rpmValue = 2750;
  setGlobalValue(F_RPM, 0.0f);

  readHighValues();

  TEST_ASSERT_EQUAL_FLOAT(2750.0f, getGlobalValue(F_RPM));
}

void test_readHighValues_does_not_touch_medium_rate_fields(void) {
  // Seed medium-rate fields with distinctive sentinels.
  setGlobalValue(F_COOLANT_TEMP, 91.0f);
  setGlobalValue(F_OIL_TEMP, 82.0f);
  setGlobalValue(F_INTAKE_TEMP, 33.0f);
  setGlobalValue(F_FUEL, 512.0f);
  setGlobalValue(F_VOLTS, 13.8f);

  readHighValues();

  TEST_ASSERT_EQUAL_FLOAT(91.0f, getGlobalValue(F_COOLANT_TEMP));
  TEST_ASSERT_EQUAL_FLOAT(82.0f, getGlobalValue(F_OIL_TEMP));
  TEST_ASSERT_EQUAL_FLOAT(33.0f, getGlobalValue(F_INTAKE_TEMP));
  TEST_ASSERT_EQUAL_FLOAT(512.0f, getGlobalValue(F_FUEL));
  TEST_ASSERT_EQUAL_FLOAT(13.8f, getGlobalValue(F_VOLTS));
}

// ── readThrottle - ADC-based mapping ─────────────────────────────────────────

void test_throttle_adc_at_idle_gives_zero(void) {
  /*
   * ADC = THROTTLE_MAX -> initialVal = THROTTLE_MAX - THROTTLE_MIN = maxVal
   * result = maxVal / (maxVal / PWM_RESOLUTION) = PWM_RESOLUTION
   * return = abs(PWM_RESOLUTION - PWM_RESOLUTION) = 0
   *
   * Fiesta's ADC policy applies RP2040 compensation to each sample. For values
   * well inside the linear range of hal_adc_compensate_rp2040_12bit, the output
   * is the input. THROTTLE_MAX = 3730 > 3584, so
   * hal_adc_compensate_rp2040_12bit adds 32 -> 3762. We inject 3730 and accept
   * the result as "near 0" within tolerance.
   */
  hal_mock_adc_inject(ADC_SENSORS_PIN, THROTTLE_MAX);
  int val = readThrottle();
  TEST_ASSERT_INT_WITHIN(50, 0, val);
}

void test_throttle_adc_at_full_gives_max(void) {
  /*
   * ADC = THROTTLE_MIN -> initialVal = 0
   * result = 0; return = abs(0 - PWM_RESOLUTION) = PWM_RESOLUTION
   * hal_adc_compensate_rp2040_12bit(1795): 1795 is above 1536 -> adds 16 ->
   * 1811. 1811 - THROTTLE_MIN (1795) = 16; result = 16 / divider ≈ 16 abs(16 -
   * 2047) ≈ 2031 - near PWM_RESOLUTION, within tolerance.
   */
  hal_mock_adc_inject(ADC_SENSORS_PIN, THROTTLE_MIN);
  int val = readThrottle();
  TEST_ASSERT_INT_WITHIN(100, PWM_RESOLUTION, val);
}

void test_throttle_unreadable_input_gives_zero_demand(void) {
  /*
   * hal_adc_read() reports an input it cannot read now as a negative value.
   * The inverted mapping would turn a zero stand-in into full throttle, so
   * an unreadable pedal must read as no demand.
   */
  hal_mock_adc_inject(ADC_SENSORS_PIN, -1);
  int val = readThrottle();
  hal_mock_adc_inject(ADC_SENSORS_PIN, THROTTLE_MAX);
  TEST_ASSERT_EQUAL_INT(0, val);
}

// ── scan-derived timings and coverage ────────────────────────────────────────

void test_scan_timings_follow_the_frame_period(void) {
  // Without a scan the reads convert live: the analog settle alone, ten
  // microseconds between samples and nothing left uncovered.
  TEST_ASSERT_FALSE(hal_adc_scan_is_running());
  TEST_ASSERT_EQUAL_UINT32(SENSORS_MUX_ANALOG_SETTLE_US, sensors_muxSettleUs());
  TEST_ASSERT_EQUAL_UINT16(10u, sensors_adcSampleDelayUs());
  TEST_ASSERT_TRUE(sensors_scanCoversInputs());

  // The ECU's own scan carries the shunt, the mux and the supply: the settle
  // grows by two frames and the samples are spaced one frame apart.
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStart());
  const uint32_t frameUs = (hal_adc_scan_frame_period_ns() + 999u) / 1000u;
  TEST_ASSERT_TRUE(frameUs > 10u);
  TEST_ASSERT_EQUAL_UINT32(SENSORS_MUX_ANALOG_SETTLE_US + (2u * frameUs),
                           sensors_muxSettleUs());
  TEST_ASSERT_EQUAL_UINT16((uint16_t)frameUs, sensors_adcSampleDelayUs());
  TEST_ASSERT_TRUE(sensors_scanCoversInputs());
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStop());
}

void test_scan_without_the_sensor_inputs_is_reported_uncovered(void) {
  static uint16_t buffer[2u * 4u * 1u];
  hal_adc_scan_config_t config = {};
  config.pins[0] = ADC_VP37_CURRENT_PIN;
  config.pin_count = 1u;
  config.conversion_period_ns = 8000u;
  config.buffer = buffer;
  config.block_frames = 4u;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_scan_start(&config));
  TEST_ASSERT_FALSE(sensors_scanCoversInputs());
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_adc_scan_stop());
  TEST_ASSERT_TRUE(sensors_scanCoversInputs());
}

// ── main
// ──────────────────────────────────────────────────────────────────────

int main(void) {
  hal_i2c_init(4, 5, 400000);
  initSensors();
  initI2C();        // required: initializes i2cBusMutex used by pcf8574_*
  dtcManagerInit(); // required: pcf8574_* calls dtcManagerSetActive()
  dtcManagerClearAll();

  UNITY_BEGIN();

  RUN_TEST(test_throttle_percentage_zero);
  RUN_TEST(test_throttle_percentage_full);
  RUN_TEST(test_throttle_percentage_midpoint);
  RUN_TEST(test_throttle_percentage_quarter);
  RUN_TEST(test_throttle_legacy_percentage_retains_integer_conversion);
  RUN_TEST(test_driver_demand_first_sample_retains_fractional_resolution);
  RUN_TEST(test_driver_demand_filters_steps_without_delaying_raw_cache);
  RUN_TEST(test_driver_demand_rejects_sub_deadband_adc_jitter);
  RUN_TEST(
      test_driver_demand_reaches_endpoints_and_releases_zero_or_failed_input);
  RUN_TEST(test_driver_demand_uses_elapsed_sample_time_across_clock_wrap);
  RUN_TEST(test_driver_demand_reset_and_getter_do_not_reuse_or_advance_filter);

  RUN_TEST(test_engine_load_zero_when_rpm_zero);
  RUN_TEST(test_engine_load_zero_when_pressure_zero);
  RUN_TEST(test_engine_load_full_at_max_rpm_and_pressure);
  RUN_TEST(test_engine_load_half_at_half_rpm);
  RUN_TEST(test_engine_load_half_at_half_pressure);
  RUN_TEST(test_engine_load_clamped_to_100);
  RUN_TEST(test_engine_load_never_negative);

  RUN_TEST(test_dpf_not_regenerating_by_default);
  RUN_TEST(test_dpf_regenerating_when_flag_set);
  RUN_TEST(test_dpf_not_regenerating_after_flag_clear);
  RUN_TEST(test_get_global_value_invalid_index_returns_zero);
  RUN_TEST(test_set_global_value_invalid_index_does_not_modify_valid_slot);
  RUN_TEST(test_init_spi_does_not_drive_vp37_current_sense_pin);
  RUN_TEST(test_internal_throttle_helper_maps_min_to_full_scale);
  RUN_TEST(test_internal_throttle_helper_maps_max_to_zero);
  RUN_TEST(test_internal_throttle_helper_clamps_below_min);
  RUN_TEST(test_internal_throttle_helper_clamps_above_max);
  RUN_TEST(test_internal_engine_load_helper_rounds_half_up);
  RUN_TEST(test_internal_engine_load_helper_clamps_negative_to_zero);
  RUN_TEST(test_internal_engine_load_helper_clamps_overflow_to_hundred);

  RUN_TEST(test_throttle_adc_at_idle_gives_zero);
  RUN_TEST(test_throttle_adc_at_full_gives_max);
  RUN_TEST(test_throttle_unreadable_input_gives_zero_demand);
  RUN_TEST(test_scan_timings_follow_the_frame_period);
  RUN_TEST(test_scan_without_the_sensor_inputs_is_reported_uncovered);

  RUN_TEST(test_pcf8574_write_targets_expected_address);
  RUN_TEST(test_pcf8574_write_changes_address_free_after_release);
  RUN_TEST(test_pcf8574_write_invalid_pin_is_noop);
  RUN_TEST(test_pcf8574_read_returns_set_bit);
  RUN_TEST(test_pcf8574_read_returns_cleared_bit);
  RUN_TEST(test_pcf8574_read_invalid_pin_returns_false);

  RUN_TEST(test_readHighValues_refreshes_rpm_from_instance);
  RUN_TEST(test_readHighValues_does_not_touch_medium_rate_fields);
  RUN_TEST(
      test_throttle_poll_updates_only_its_cache_and_getters_do_not_resample);
  RUN_TEST(test_readHighValues_preserves_the_independently_sampled_throttle);

  return UNITY_END();
}
