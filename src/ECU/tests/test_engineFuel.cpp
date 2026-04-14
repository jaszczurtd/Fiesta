#include "unity.h"
#include "engineFuel.h"
#include "sensors.h"
#include "hal/impl/.mock/hal_mock.h"

static int expectedFuelFromRawAdc(int rawAdc) {
    int compensated = adcCompe(rawAdc);
    return abs(compensated - FUEL_MIN);
}

void setUp(void) {
    hal_mock_set_millis(0);
    initSensors();
    initFuelMeasurement();
    hal_mock_adc_inject(ADC_SENSORS_PIN, 0);
}

void tearDown(void) {}

void test_readFuel_first_read_returns_expected_value(void) {
    hal_mock_adc_inject(ADC_SENSORS_PIN, 1000);

    int got = (int)readFuel();

    TEST_ASSERT_EQUAL_INT(expectedFuelFromRawAdc(1000), got);
}

void test_readFuel_before_next_window_keeps_last_value(void) {
    hal_mock_adc_inject(ADC_SENSORS_PIN, 1000);
    int first = (int)readFuel();

    hal_mock_adc_inject(ADC_SENSORS_PIN, 1200);
    int second = (int)readFuel();

    TEST_ASSERT_EQUAL_INT(first, second);
}

void test_readFuel_after_time_window_recomputes_average(void) {
    hal_mock_adc_inject(ADC_SENSORS_PIN, 1000);
    int first = (int)readFuel();
    TEST_ASSERT_EQUAL_INT(expectedFuelFromRawAdc(1000), first);

    hal_mock_advance_millis(7000);

    hal_mock_adc_inject(ADC_SENSORS_PIN, 1200);
    int second = (int)readFuel();

    int expectedAvg = (expectedFuelFromRawAdc(1000) + expectedFuelFromRawAdc(1200)) / 2;
    TEST_ASSERT_EQUAL_INT(expectedAvg, second);
}

void test_readFuel_index_wrap_after_full_buffer(void) {
    const int expected = expectedFuelFromRawAdc(1000);

    for(int i = 0; i < (FUEL_MAX_SAMPLES + 4); i++) {
        hal_mock_adc_inject(ADC_SENSORS_PIN, 1000);
        int got = (int)readFuel();
        TEST_ASSERT_EQUAL_INT(expected, got);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_readFuel_first_read_returns_expected_value);
    RUN_TEST(test_readFuel_before_next_window_keeps_last_value);
    RUN_TEST(test_readFuel_after_time_window_recomputes_average);
    RUN_TEST(test_readFuel_index_wrap_after_full_buffer);
    return UNITY_END();
}
