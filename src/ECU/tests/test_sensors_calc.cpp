#include "unity.h"
#include "sensors.h"
#include "testable/sensors_testable.h"
#include "hal/impl/.mock/hal_mock.h"

/*
 * sensors.cpp calculation tests — functions that operate purely on global
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
 *   → low ADC (= THROTTLE_MIN) → full throttle value = PWM_RESOLUTION
 *   → high ADC (= THROTTLE_MAX) → idle = 0
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
    /* PWM_RESOLUTION / 2 ≈ 1023 → (1023*100)/2047 ≈ 49 % */
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
    /* map = 255, rpm = 2500/5000 = 0.5 → load = 50 */
    setGlobalValue(F_RPM, (float)(RPM_MAX_EVER / 2));
    setGlobalValue(F_PRESSURE, 2.55f);
    TEST_ASSERT_INT_WITHIN(2, 50, getPercentageEngineLoad());
}

void test_engine_load_half_at_half_pressure(void) {
    /* map = 127.5/255 = 0.5, rpm full → load = 50 */
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

// ── readThrottle — ADC-based mapping ─────────────────────────────────────────

void test_throttle_adc_at_idle_gives_zero(void) {
    /*
     * ADC = THROTTLE_MAX → initialVal = THROTTLE_MAX - THROTTLE_MIN = maxVal
     * result = maxVal / (maxVal / PWM_RESOLUTION) = PWM_RESOLUTION
     * return = abs(PWM_RESOLUTION - PWM_RESOLUTION) = 0
     *
     * getAverageValueFrom() applies adcCompe() to each sample.  For values
     * well inside the linear range of adcCompe, the output is the input.
     * THROTTLE_MAX = 3730 > 3584, so adcCompe adds 32 → 3762.
     * We inject 3730 and accept the result as "near 0" within tolerance.
     */
    hal_mock_adc_inject(ADC_SENSORS_PIN, THROTTLE_MAX);
    int val = readThrottle();
    TEST_ASSERT_INT_WITHIN(50, 0, val);
}

void test_throttle_adc_at_full_gives_max(void) {
    /*
     * ADC = THROTTLE_MIN → initialVal = 0
     * result = 0; return = abs(0 - PWM_RESOLUTION) = PWM_RESOLUTION
     * adcCompe(1795): 1795 is above 1536 → adds 16 → 1811.
     * 1811 - THROTTLE_MIN (1795) = 16; result = 16 / divider ≈ 16
     * abs(16 - 2047) ≈ 2031 — near PWM_RESOLUTION, within tolerance.
     */
    hal_mock_adc_inject(ADC_SENSORS_PIN, THROTTLE_MIN);
    int val = readThrottle();
    TEST_ASSERT_INT_WITHIN(100, PWM_RESOLUTION, val);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(void) {
    initSensors();

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

    return UNITY_END();
}
