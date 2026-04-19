/**
 * @file test_led.cpp
 * @brief Host-side tests for Adjustometer LED state machine
 *        (sequence-based status display).
 */

#include "utils/unity.h"
#include "sensors.h"
#include "led.h"
#include "hal/impl/.mock/hal_mock.h"

/* ── Helpers ───────────────────────────────────────────────────────────────── */

#define LED_BLINK_STATUS_MS  500U
#define LED_BLINK_NO_OSC_MS  125U

static void simulatePulses(uint32_t count, uint32_t freqHz) {
    const uint32_t periodUs = 1000000U / freqHz;
    for (uint32_t i = 0; i < count; i++) {
        hal_mock_advance_micros(periodUs);
        hal_mock_gpio_fire_interrupt(PIO_INTERRUPT_HALL);
    }
}

static void lockBaseline(uint32_t freqHz) {
    const uint32_t pulseWindow = 128U;
    const uint32_t periodUs  = 1000000U / freqHz;
    const uint32_t windowUs  = pulseWindow * periodUs;
    const uint32_t totalTimeUs = (ADJUSTOMETER_BASELINE_MAX_TIME_MS +
                                  ADJUSTOMETER_BASELINE_VERIFY_MS) * 1000UL;
    const uint32_t windows   = (totalTimeUs / windowUs) + 5U;
    simulatePulses(windows * pulseWindow, freqHz);
}

/** Inject ADC so fuel temp appears OK or broken. */
static void injectFuelTemp(bool broken) {
    hal_mock_adc_inject(ADC_FUEL_TEMP_PIN, broken ? 4090 : 2000);
}

/** Inject ADC so voltage is normal. */
static void injectVoltageOk(void) {
    const float ratio = (float)(VDIV_R1_KOHM + VDIV_R2_KOHM) / (float)VDIV_R2_KOHM;
    int adc = (int)(12.0f / ratio * 4095.0f / 3.3f);
    hal_mock_adc_inject(ADC_VOLT_PIN, adc);
}

/** Inject ADC so voltage reads as too low (5V → below 8V threshold). */
static void injectVoltageBad(void) {
    const float ratio = (float)(VDIV_R1_KOHM + VDIV_R2_KOHM) / (float)VDIV_R2_KOHM;
    int adc = (int)(5.0f / ratio * 4095.0f / 3.3f);
    hal_mock_adc_inject(ADC_VOLT_PIN, adc);
}

/** Settle ADC EMA filters to injected values. */
static void settleAdcFilters(void) {
    for (int i = 0; i < 30; i++) {
        getFuelTemperatureRaw();
        getSupplyVoltageRaw();
    }
}

/** Bump I2C slave transaction count → LED sees "I2C active". */
static void bumpI2C(void) {
    uint8_t dummy[] = {0x00, 0x42};
    hal_mock_i2c_slave_simulate_receive(dummy, 2);
}

/**
 * Ensure signal is alive by feeding recent pulses so isSignalLost()==false.
 * Uses a small burst at the baseline frequency.
 */
static void keepSignalAlive(void) {
    simulatePulses(128, 10000);
}

/* ── Setup / Teardown ──────────────────────────────────────────────────────── */

void setUp(void) {
    hal_mock_set_micros(1);
    hal_mock_set_millis(0);
    injectFuelTemp(false);
    injectVoltageOk();
    initSensors();
    initLed();
}

void tearDown(void) {}

/* ── All-OK: steady green at half brightness ───────────────────────────────── */

void test_all_ok_steady_green(void) {
    lockBaseline(10000);
    settleAdcFilters();
    bumpI2C();
    keepSignalAlive();

    /* all-OK path doesn't check timing */
    updateLed();

    TEST_ASSERT_EQUAL(HAL_RGB_LED_GREEN, hal_mock_rgb_led_get_color());
    TEST_ASSERT_EQUAL_UINT8(15, hal_mock_rgb_led_get_brightness());
}

/* ── Signal lost: red blink 4×/s ───────────────────────────────────────────── */

void test_signal_lost_red_blink(void) {
    /* No pulses → signal lost.  Advance millis past blink period. */
    hal_mock_advance_micros(300000);
    hal_mock_advance_millis(200);
    updateLed();
    hal_rgb_led_color_t c1 = hal_mock_rgb_led_get_color();

    hal_mock_advance_millis(LED_BLINK_NO_OSC_MS);
    updateLed();

    hal_mock_advance_millis(LED_BLINK_NO_OSC_MS);
    updateLed();
    hal_rgb_led_color_t c3 = hal_mock_rgb_led_get_color();

    TEST_ASSERT_EQUAL(HAL_RGB_LED_RED, c1);
    TEST_ASSERT_EQUAL(HAL_RGB_LED_RED, c3);
}

/* ── No I2C only: red → green cycle ────────────────────────────────────────── */

void test_no_i2c_red_green_cycle(void) {
    lockBaseline(10000);
    settleAdcFilters();
    /* Advance past LED_I2C_TIMEOUT_MS (2000 ms) so noI2C triggers,
     * then keep signal alive so isSignalLost() stays false. */
    hal_mock_advance_millis(2500);
    keepSignalAlive();
    updateLed();
    hal_rgb_led_color_t first = hal_mock_rgb_led_get_color();

    hal_mock_advance_millis(LED_BLINK_STATUS_MS);
    keepSignalAlive();
    updateLed();
    hal_rgb_led_color_t second = hal_mock_rgb_led_get_color();

    bool has_red   = (first == HAL_RGB_LED_RED)   || (second == HAL_RGB_LED_RED);
    bool has_green = (first == HAL_RGB_LED_GREEN) || (second == HAL_RGB_LED_GREEN);
    TEST_ASSERT_TRUE(has_red);
    TEST_ASSERT_TRUE(has_green);
}

/* ── Fuel temp broken only: purple → green cycle ───────────────────────────── */

void test_fuel_broken_purple_green_cycle(void) {
    injectFuelTemp(true);
    settleAdcFilters();
    lockBaseline(10000);
    bumpI2C();

    hal_mock_advance_millis(2000);
    keepSignalAlive();
    updateLed();
    hal_rgb_led_color_t first = hal_mock_rgb_led_get_color();

    bumpI2C();
    hal_mock_advance_millis(LED_BLINK_STATUS_MS);
    keepSignalAlive();
    updateLed();
    hal_rgb_led_color_t second = hal_mock_rgb_led_get_color();

    bool has_purple = (first == HAL_RGB_LED_PURPLE) || (second == HAL_RGB_LED_PURPLE);
    bool has_green  = (first == HAL_RGB_LED_GREEN)  || (second == HAL_RGB_LED_GREEN);
    TEST_ASSERT_TRUE(has_purple);
    TEST_ASSERT_TRUE(has_green);
}

/* ── Both faults: purple → red → green cycle ───────────────────────────────── */

void test_both_faults_three_color_cycle(void) {
    injectFuelTemp(true);
    settleAdcFilters();
    lockBaseline(10000);

    hal_mock_advance_millis(3000);
    keepSignalAlive();
    updateLed();
    hal_rgb_led_color_t c1 = hal_mock_rgb_led_get_color();

    hal_mock_advance_millis(LED_BLINK_STATUS_MS);
    keepSignalAlive();
    updateLed();
    hal_rgb_led_color_t c2 = hal_mock_rgb_led_get_color();

    hal_mock_advance_millis(LED_BLINK_STATUS_MS);
    keepSignalAlive();
    updateLed();
    hal_rgb_led_color_t c3 = hal_mock_rgb_led_get_color();

    bool has_purple = (c1 == HAL_RGB_LED_PURPLE) || (c2 == HAL_RGB_LED_PURPLE) || (c3 == HAL_RGB_LED_PURPLE);
    bool has_red    = (c1 == HAL_RGB_LED_RED)    || (c2 == HAL_RGB_LED_RED)    || (c3 == HAL_RGB_LED_RED);
    bool has_green  = (c1 == HAL_RGB_LED_GREEN)  || (c2 == HAL_RGB_LED_GREEN)  || (c3 == HAL_RGB_LED_GREEN);
    TEST_ASSERT_TRUE(has_purple);
    TEST_ASSERT_TRUE(has_red);
    TEST_ASSERT_TRUE(has_green);
}

/* ── Fault clears → returns to steady green ────────────────────────────────── */

void test_fault_clears_returns_to_green(void) {
    lockBaseline(10000);

    injectFuelTemp(true);
    settleAdcFilters();
    hal_mock_advance_millis(4000);
    keepSignalAlive();
    updateLed();
    hal_rgb_led_color_t c_fault = hal_mock_rgb_led_get_color();
    TEST_ASSERT_NOT_EQUAL(HAL_RGB_LED_GREEN, c_fault);

    injectFuelTemp(false);
    for (int i = 0; i < 30; i++) { getFuelTemperatureRaw(); getSupplyVoltageRaw(); }
    bumpI2C();
    hal_mock_advance_millis(LED_BLINK_STATUS_MS);
    keepSignalAlive();
    updateLed();

    hal_rgb_led_color_t c_ok = hal_mock_rgb_led_get_color();
    TEST_ASSERT_EQUAL(HAL_RGB_LED_GREEN, c_ok);
    TEST_ASSERT_EQUAL_UINT8(15, hal_mock_rgb_led_get_brightness());
}

/* ── Voltage bad only: yellow → green cycle ────────────────────────────────── */

void test_voltage_bad_yellow_green_cycle(void) {
    injectVoltageBad();
    settleAdcFilters();
    lockBaseline(10000);
    bumpI2C();

    hal_mock_advance_millis(2000);
    keepSignalAlive();
    updateLed();
    hal_rgb_led_color_t first = hal_mock_rgb_led_get_color();

    bumpI2C();
    hal_mock_advance_millis(LED_BLINK_STATUS_MS);
    keepSignalAlive();
    updateLed();
    hal_rgb_led_color_t second = hal_mock_rgb_led_get_color();

    bool has_yellow = (first == HAL_RGB_LED_YELLOW) || (second == HAL_RGB_LED_YELLOW);
    bool has_green  = (first == HAL_RGB_LED_GREEN)  || (second == HAL_RGB_LED_GREEN);
    TEST_ASSERT_TRUE(has_yellow);
    TEST_ASSERT_TRUE(has_green);
}

/* ── Voltage bad clears → returns to green ─────────────────────────────────── */

void test_voltage_bad_clears_returns_to_green(void) {
    lockBaseline(10000);
    injectVoltageBad();
    settleAdcFilters();
    bumpI2C();
    hal_mock_advance_millis(2000);
    keepSignalAlive();
    updateLed();
    TEST_ASSERT_NOT_EQUAL(HAL_RGB_LED_GREEN, hal_mock_rgb_led_get_color());

    injectVoltageOk();
    for (int i = 0; i < 30; i++) { getFuelTemperatureRaw(); getSupplyVoltageRaw(); }
    bumpI2C();
    hal_mock_advance_millis(LED_BLINK_STATUS_MS);
    keepSignalAlive();
    updateLed();

    TEST_ASSERT_EQUAL(HAL_RGB_LED_GREEN, hal_mock_rgb_led_get_color());
}

/* ── Runner ───────────────────────────────────────────────────────────────── */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_all_ok_steady_green);
    RUN_TEST(test_signal_lost_red_blink);
    RUN_TEST(test_no_i2c_red_green_cycle);
    RUN_TEST(test_fuel_broken_purple_green_cycle);
    RUN_TEST(test_both_faults_three_color_cycle);
    RUN_TEST(test_fault_clears_returns_to_green);
    RUN_TEST(test_voltage_bad_yellow_green_cycle);
    RUN_TEST(test_voltage_bad_clears_returns_to_green);
    return UNITY_END();
}
