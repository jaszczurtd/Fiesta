#include "unity.h"
#include "sensors.h"
#include "dtcManager.h"
#include "rpm.h"
#include "testable/sensors_testable.h"
#include "hal/impl/.mock/hal_mock.h"

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
    hal_mock_set_millis(0);
    hal_mock_adc_inject(ADC_SENSORS_PIN, 0);
    /* Zero all global sensor values */
    for (int i = 0; i < F_LAST; i++) {
        setGlobalValue(i, 0.0f);
    }
}

void tearDown(void) {}

// ── getThrottlePercentage ─────────────────────────────────────────────────────

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

// ── getPercentageEngineLoad ───────────────────────────────────────────────────

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

// ── isDPFRegenerating ─────────────────────────────────────────────────────────

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

// ── global value index guards ──────────────────────────────────────────────────

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

// ── internal testable helpers ───────────────────────────────────────────────

void test_internal_throttle_helper_maps_min_to_full_scale(void) {
    TEST_ASSERT_EQUAL_INT(PWM_RESOLUTION, sensors_computeThrottlePositionFromRaw(THROTTLE_MIN));
}

void test_internal_throttle_helper_maps_max_to_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, sensors_computeThrottlePositionFromRaw(THROTTLE_MAX));
}

void test_internal_throttle_helper_clamps_below_min(void) {
    TEST_ASSERT_EQUAL_INT(PWM_RESOLUTION, sensors_computeThrottlePositionFromRaw(THROTTLE_MIN - 200));
}

void test_internal_throttle_helper_clamps_above_max(void) {
    TEST_ASSERT_EQUAL_INT(0, sensors_computeThrottlePositionFromRaw(THROTTLE_MAX + 200));
}

void test_internal_engine_load_helper_rounds_half_up(void) {
    TEST_ASSERT_EQUAL_INT(67, sensors_calculateEngineLoadFromValues(2.55f, 3325.0f));
}

void test_internal_engine_load_helper_clamps_negative_to_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, sensors_calculateEngineLoadFromValues(-1.0f, (float)RPM_MAX_EVER));
}

void test_internal_engine_load_helper_clamps_overflow_to_hundred(void) {
    TEST_ASSERT_EQUAL_INT(100, sensors_calculateEngineLoadFromValues(5.0f, (float)(RPM_MAX_EVER * 2)));
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

    pcf8574_write(8, true);  // pin > 7 -> invalid, early return
    TEST_ASSERT_EQUAL_UINT8(0x11, hal_mock_i2c_get_last_addr());
}

void test_pcf8574_read_returns_set_bit(void) {
    // PCF8574 reports the full port byte; bit 3 set -> pcf8574_read(3) == true.
    hal_mock_i2c_set_busy(false);
    const uint8_t rx[] = { (uint8_t)(1u << 3) };
    hal_mock_i2c_inject_rx(rx, 1);

    TEST_ASSERT_TRUE(pcf8574_read(3));
}

void test_pcf8574_read_returns_cleared_bit(void) {
    hal_mock_i2c_set_busy(false);
    const uint8_t rx[] = { 0x00 };
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
// refresh F_RPM, F_THROTTLE_POS, F_PRESSURE, F_GPS_CAR_SPEED and
// F_CALCULATED_ENGINE_LOAD on every tick, and must NOT touch fields owned by
// readMediumValues() (F_COOLANT_TEMP, F_OIL_TEMP, F_INTAKE_TEMP, F_FUEL,
// F_VOLTS). These tests guard the explicit setGlobalValue() wiring against
// accidental removal after the reflectionValueFields cleanup.

void test_readHighValues_refreshes_rpm_from_instance(void) {
    getRPMInstance()->rpmValue = 2750;
    setGlobalValue(F_RPM, 0.0f);

    readHighValues();

    TEST_ASSERT_EQUAL_FLOAT(2750.0f, getGlobalValue(F_RPM));
}

void test_readHighValues_does_not_touch_medium_rate_fields(void) {
    // Seed medium-rate fields with distinctive sentinels.
    setGlobalValue(F_COOLANT_TEMP, 91.0f);
    setGlobalValue(F_OIL_TEMP,     82.0f);
    setGlobalValue(F_INTAKE_TEMP,  33.0f);
    setGlobalValue(F_FUEL,         512.0f);
    setGlobalValue(F_VOLTS,        13.8f);

    readHighValues();

    TEST_ASSERT_EQUAL_FLOAT(91.0f,  getGlobalValue(F_COOLANT_TEMP));
    TEST_ASSERT_EQUAL_FLOAT(82.0f,  getGlobalValue(F_OIL_TEMP));
    TEST_ASSERT_EQUAL_FLOAT(33.0f,  getGlobalValue(F_INTAKE_TEMP));
    TEST_ASSERT_EQUAL_FLOAT(512.0f, getGlobalValue(F_FUEL));
    TEST_ASSERT_EQUAL_FLOAT(13.8f,  getGlobalValue(F_VOLTS));
}

// ── readThrottle - ADC-based mapping ─────────────────────────────────────────

void test_throttle_adc_at_idle_gives_zero(void) {
    /*
     * ADC = THROTTLE_MAX -> initialVal = THROTTLE_MAX - THROTTLE_MIN = maxVal
     * result = maxVal / (maxVal / PWM_RESOLUTION) = PWM_RESOLUTION
     * return = abs(PWM_RESOLUTION - PWM_RESOLUTION) = 0
     *
     * getAverageValueFrom() applies adcCompe() to each sample.  For values
     * well inside the linear range of adcCompe, the output is the input.
     * THROTTLE_MAX = 3730 > 3584, so adcCompe adds 32 -> 3762.
     * We inject 3730 and accept the result as "near 0" within tolerance.
     */
    hal_mock_adc_inject(ADC_SENSORS_PIN, THROTTLE_MAX);
    int val = readThrottle();
    TEST_ASSERT_INT_WITHIN(50, 0, val);
}

void test_throttle_adc_at_full_gives_max(void) {
    /*
     * ADC = THROTTLE_MIN -> initialVal = 0
     * result = 0; return = abs(0 - PWM_RESOLUTION) = PWM_RESOLUTION
     * adcCompe(1795): 1795 is above 1536 -> adds 16 -> 1811.
     * 1811 - THROTTLE_MIN (1795) = 16; result = 16 / divider ≈ 16
     * abs(16 - 2047) ≈ 2031 - near PWM_RESOLUTION, within tolerance.
     */
    hal_mock_adc_inject(ADC_SENSORS_PIN, THROTTLE_MIN);
    int val = readThrottle();
    TEST_ASSERT_INT_WITHIN(100, PWM_RESOLUTION, val);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(void) {
    hal_i2c_init(4, 5, 400000);
    initSensors();
    initI2C();          // required: initializes i2cBusMutex used by pcf8574_*
    dtcManagerInit();   // required: pcf8574_* calls dtcManagerSetActive()
    dtcManagerClearAll();

    UNITY_BEGIN();

    RUN_TEST(test_throttle_percentage_zero);
    RUN_TEST(test_throttle_percentage_full);
    RUN_TEST(test_throttle_percentage_midpoint);
    RUN_TEST(test_throttle_percentage_quarter);

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
    RUN_TEST(test_internal_throttle_helper_maps_min_to_full_scale);
    RUN_TEST(test_internal_throttle_helper_maps_max_to_zero);
    RUN_TEST(test_internal_throttle_helper_clamps_below_min);
    RUN_TEST(test_internal_throttle_helper_clamps_above_max);
    RUN_TEST(test_internal_engine_load_helper_rounds_half_up);
    RUN_TEST(test_internal_engine_load_helper_clamps_negative_to_zero);
    RUN_TEST(test_internal_engine_load_helper_clamps_overflow_to_hundred);

    RUN_TEST(test_throttle_adc_at_idle_gives_zero);
    RUN_TEST(test_throttle_adc_at_full_gives_max);

    RUN_TEST(test_pcf8574_write_targets_expected_address);
    RUN_TEST(test_pcf8574_write_changes_address_free_after_release);
    RUN_TEST(test_pcf8574_write_invalid_pin_is_noop);
    RUN_TEST(test_pcf8574_read_returns_set_bit);
    RUN_TEST(test_pcf8574_read_returns_cleared_bit);
    RUN_TEST(test_pcf8574_read_invalid_pin_returns_false);

    RUN_TEST(test_readHighValues_refreshes_rpm_from_instance);
    RUN_TEST(test_readHighValues_does_not_touch_medium_rate_fields);

    return UNITY_END();
}
