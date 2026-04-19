/**
 * @file test_sensors.cpp
 * @brief Host-side tests for Adjustometer sensor logic (ISR, baseline,
 *        thermal compensation, zero-hold, status bitmask).
 */

#include "utils/unity.h"
#include "sensors.h"
#include "hal/impl/.mock/hal_mock.h"

/* ── Helpers ───────────────────────────────────────────────────────────────── */

static void simulatePulses(uint32_t count, uint32_t freqHz) {
    const uint32_t periodUs = 1000000U / freqHz;
    for (uint32_t i = 0; i < count; i++) {
        hal_mock_advance_micros(periodUs);
        hal_mock_gpio_fire_interrupt(PIO_INTERRUPT_HALL);
    }
}

/**
 * Lock baseline at freqHz by driving enough pulses to exceed
 * ADJUSTOMETER_BASELINE_MAX_TIME_MS (force convergence) plus
 * ADJUSTOMETER_BASELINE_VERIFY_MS (post-convergence verification).
 * Pulse window is 128 (internal constant in sensors.c).
 */
static void lockBaseline(uint32_t freqHz) {
    const uint32_t pulseWindow = 128U;
    const uint32_t periodUs  = 1000000U / freqHz;
    const uint32_t windowUs  = pulseWindow * periodUs;
    const uint32_t totalTimeUs = (ADJUSTOMETER_BASELINE_MAX_TIME_MS +
                                  ADJUSTOMETER_BASELINE_VERIFY_MS) * 1000UL;
    const uint32_t windows   = (totalTimeUs / windowUs) + 5U;
    simulatePulses(windows * pulseWindow, freqHz);
}

/**
 * Inject ADC for supply voltage at given tenths-of-volt.
 * Accounts for adcCompe offset (+8..+32) by back-calculating.
 */
static void injectVoltage(int tenthsOfVolt) {
    const float ratio = (float)(VDIV_R1_KOHM + VDIV_R2_KOHM) / (float)VDIV_R2_KOHM;
    float volts = (float)tenthsOfVolt / 10.0f;
    int adc = (int)(volts / ratio * 4095.0f / 3.3f);
    if (adc < 0) adc = 0;
    hal_mock_adc_inject(ADC_VOLT_PIN, adc);
}

/**
 * Inject ADC for fuel temperature sensor.
 * broken=true → ADC near max → ntcToTemp returns negative → 0.
 * broken=false → ADC mid-range → ntcToTemp returns positive.
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
        getFuelTemperatureRaw();
        getSupplyVoltageRaw();
    }
}

/* ── Setup / Teardown ──────────────────────────────────────────────────────── */

void setUp(void) {
    hal_mock_set_micros(1);
    hal_mock_set_millis(0);
    injectVoltage(120);
    injectFuelTemp(false);
    initSensors();
}

void tearDown(void) {}

/* ── Frequency computation ─────────────────────────────────────────────────── */

void test_no_pulses_returns_zero(void) {
    TEST_ASSERT_EQUAL_INT32(0, getAdjustometerPulses());
}

void test_pulses_before_baseline_returns_zero(void) {
    simulatePulses(256, 10000);
    TEST_ASSERT_EQUAL_INT32(0, getAdjustometerPulses());
}

/* ── Baseline lock ─────────────────────────────────────────────────────────── */

void test_baseline_locks_after_stable_signal(void) {
    lockBaseline(10000);
    simulatePulses(256, 10000);
    int32_t pulse = getAdjustometerPulses();
    TEST_ASSERT_INT32_WITHIN(15, 0, pulse);
}

void test_frequency_shift_produces_nonzero_pulse(void) {
    lockBaseline(10000);
    /* Shift from 10000 Hz baseline to ~11111 Hz (period 90us) */
    simulatePulses(2048, 11111);
    int32_t pulse = getAdjustometerPulses();
    TEST_ASSERT_TRUE(pulse > 200);
}

/* ── Zero-hold hysteresis ──────────────────────────────────────────────────── */

void test_small_shift_within_zero_hold(void) {
    /* Use 1000 Hz where 1 us period change → small Hz shift */
    lockBaseline(1000);
    /* Shift to ~1010 Hz (period 990us): Δ=10 Hz < ENTER=20 Hz */
    simulatePulses(512, 1010);
    int32_t pulse = getAdjustometerPulses();
    TEST_ASSERT_EQUAL_INT32(0, pulse);
}

/* ── Signal loss ───────────────────────────────────────────────────────────── */

void test_signal_lost_returns_zero(void) {
    lockBaseline(10000);
    simulatePulses(256, 10000);
    /* Advance time well past signal loss timeout without any pulses */
    hal_mock_advance_micros(ADJUSTOMETER_SIGNAL_LOSS_MAX_US + 100000U);
    TEST_ASSERT_EQUAL_INT32(0, getAdjustometerPulses());
}

/* ── Status bitmask ────────────────────────────────────────────────────────── */

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
    simulatePulses(128, 10000);
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
    injectVoltage(50);   /* 5.0 V — well below 8.0 V threshold */
    settleAdcFilters();
    uint8_t status = getAdjustometerStatus();
    TEST_ASSERT_BITS_HIGH(ADJ_STATUS_VOLTAGE_BAD, status);
}

void test_status_voltage_too_high(void) {
    injectVoltage(200);  /* 20.0 V — well above 15.0 V threshold */
    settleAdcFilters();
    uint8_t status = getAdjustometerStatus();
    TEST_ASSERT_BITS_HIGH(ADJ_STATUS_VOLTAGE_BAD, status);
}

void test_status_voltage_ok(void) {
    injectVoltage(120);  /* 12.0 V */
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

/* ── Thermal compensation guard ────────────────────────────────────────────── */

void test_thermal_comp_skipped_when_sensor_broken(void) {
    injectFuelTemp(false);
    settleAdcFilters();
    lockBaseline(10000);
    simulatePulses(256, 10000);
    int32_t pulseOk = getAdjustometerPulses();

    /* settleAdcFilters() advances mock micros (~2400 us via hal_delay_us),
     * creating a timing gap that corrupts the first ISR window.
     * Feed enough extra pulses (16 windows) to flush the gap and let
     * the EMA reconverge to the true frequency. */
    injectFuelTemp(true);
    settleAdcFilters();
    simulatePulses(4096 + 256, 10000);
    int32_t pulseBroken = getAdjustometerPulses();

    TEST_ASSERT_INT32_WITHIN(15, 0, pulseOk);
    TEST_ASSERT_INT32_WITHIN(15, 0, pulseBroken);
}

/* ── NaN immunity (regression for adcCompe > ADC_MAX) ──────────────────────── */

/**
 * When ADC reads near max (4090), adcCompe pushes it to ~4122 (> ADC_MAX=4095).
 * ntcToTemp computes 4095/4122-1 → negative → log(negative) → NaN.
 * Without the isnan guard, NaN permanently poisons the EMA filter.
 * This test verifies the filter recovers after a broken sensor phase.
 */
void test_nan_recovery_after_broken_sensor(void) {
    injectFuelTemp(true);   /* ADC=4090 → adcCompe > 4095 → NaN from ntcToTemp */
    settleAdcFilters();
    uint8_t broken = getFuelTemperatureRaw();
    TEST_ASSERT_EQUAL_UINT8(ADJ_FUEL_TEMP_SENSOR_BROKEN, broken);

    injectFuelTemp(false);  /* ADC=2000 → ntcToTemp ≈ 13°C */
    for (int i = 0; i < 30; i++) getFuelTemperatureRaw();
    uint8_t recovered = getFuelTemperatureRaw();
    TEST_ASSERT_TRUE(recovered > 0);  /* Must escape NaN trap */
}

/* ── getAdjustometerStatus must not mutate temperature EMA ─────────────────── */

/**
 * Calling getAdjustometerStatus() repeatedly should not change the
 * fuel temperature reading — it reads adjustometerSharedFuelTemp
 * atomically instead of calling getFuelTemperatureRaw().
 *
 * We inject a moderate ADC change (not broken sensor) so that
 * one EMA step produces a small shift, while 50 hidden EMA steps
 * (the old side-effect) would produce a large drift.
 */
void test_status_does_not_mutate_fuel_temp_ema(void) {
    injectFuelTemp(false);  /* ADC=2000 → ~13°C */
    settleAdcFilters();
    uint8_t tempBefore = getFuelTemperatureRaw();
    TEST_ASSERT_TRUE(tempBefore > 0);

    /* Shift ADC to a significantly different (but valid) value */
    hal_mock_adc_inject(ADC_FUEL_TEMP_PIN, 1000);
    /* 50 status calls — must not advance the temperature EMA */
    for (int i = 0; i < 50; i++) getAdjustometerStatus();

    /* First getFuelTemperatureRaw after 50 status calls:
     * only 1 EMA step should happen, so the shift must be small. */
    uint8_t tempAfter = getFuelTemperatureRaw();
    /* With the old side-effect bug: tempAfter would be far from tempBefore
     * (50 EMA steps toward the new ADC value). Fixed: only 1 step. */
    TEST_ASSERT_INT_WITHIN(5, tempBefore, tempAfter);
}

/* ── Signal-loss consistency ───────────────────────────────────────────────── */

/**
 * getAdjustometerPulses() and getAdjustometerStatus() must agree on
 * signal-loss: if pulses returns 0 due to signal loss, status must
 * have SIGNAL_LOST bit set, and vice versa.
 */
void test_signal_loss_consistency_pulses_vs_status(void) {
    lockBaseline(10000);
    simulatePulses(256, 10000);

    /* Signal alive — both should agree */
    TEST_ASSERT_TRUE(getAdjustometerPulses() >= 0);
    TEST_ASSERT_BITS_LOW(ADJ_STATUS_SIGNAL_LOST, getAdjustometerStatus());

    /* Signal lost */
    hal_mock_advance_micros(ADJUSTOMETER_SIGNAL_LOSS_MAX_US + 100000U);
    int32_t pulses = getAdjustometerPulses();
    uint8_t status = getAdjustometerStatus();
    TEST_ASSERT_EQUAL_INT32(0, pulses);
    TEST_ASSERT_BITS_HIGH(ADJ_STATUS_SIGNAL_LOST, status);
}

/* ── Runner ───────────────────────────────────────────────────────────────── */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_no_pulses_returns_zero);
    RUN_TEST(test_pulses_before_baseline_returns_zero);
    RUN_TEST(test_baseline_locks_after_stable_signal);
    RUN_TEST(test_frequency_shift_produces_nonzero_pulse);
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
