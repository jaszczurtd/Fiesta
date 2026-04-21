#include "unity.h"
#include "turbo.h"
#include "sensors.h"
#include "dtcManager.h"
#include "testable/turbo_testable.h"
#include "hal/hal_eeprom.h"
#include "hal/impl/.mock/hal_mock.h"

void setUp(void) {
    hal_mock_set_millis(0);
    hal_mock_eeprom_reset();
    hal_eeprom_init(HAL_EEPROM_RP2040, ECU_EEPROM_SIZE_BYTES, 0);
    initSensors();
    dtcManagerInit();
    dtcManagerClearAll();
    for (int i = 0; i < F_LAST; i++) {
        setGlobalValue(i, 0.0f);
    }
}

void tearDown(void) {}

void test_turbo_uses_last_rpm_row_when_rpm_exceeds_limit(void) {
    Turbo turbo = {0};

    setGlobalValue(F_THROTTLE_POS, (float)PWM_RESOLUTION);      // 100%
    setGlobalValue(F_PRESSURE, 1.0f);                           // below MAX_BOOST_PRESSURE
    setGlobalValue(F_RPM, (float)(RPM_MAX_EVER * 2));           // should clamp
    setGlobalValue(F_INTAKE_TEMP, (float)(MIN_TEMPERATURE_CORRECTION - 1)); // no temp correction

    Turbo_process(&turbo);

    const int expectedPressure = RPM_table[RPM_PRESCALERS - 1][N75_PERCENT_VALS - 1];
    TEST_ASSERT_EQUAL_INT(RPM_PRESCALERS - 1, turbo.RPM_index);
    TEST_ASSERT_EQUAL_INT(expectedPressure, (int)getGlobalValue(F_PRESSURE_PERCENTAGE));
    TEST_ASSERT_EQUAL_INT(percentToGivenVal((float)expectedPressure, PWM_RESOLUTION), turbo.n75);
}

void test_turbo_applies_intake_temperature_correction(void) {
    Turbo turbo = {0};

    setGlobalValue(F_THROTTLE_POS, (float)PWM_RESOLUTION); // 100%
    setGlobalValue(F_PRESSURE, 1.0f);                      // below MAX_BOOST_PRESSURE
    setGlobalValue(F_RPM, (float)RPM_MAX_EVER);            // last row
    setGlobalValue(F_INTAKE_TEMP, 40.0f);                  // correction: ((40-30)/5)+1 = 3

    Turbo_process(&turbo);

    const int basePressure = RPM_table[RPM_PRESCALERS - 1][N75_PERCENT_VALS - 1];
    const int expectedPressure = basePressure - 3;
    TEST_ASSERT_EQUAL_INT(expectedPressure, (int)getGlobalValue(F_PRESSURE_PERCENTAGE));
    TEST_ASSERT_EQUAL_INT(percentToGivenVal((float)expectedPressure, PWM_RESOLUTION), turbo.n75);
}

void test_turbo_overboost_path_never_sets_negative_pressure_percentage(void) {
    Turbo turbo = {0};

    setGlobalValue(F_PRESSURE, (float)(MAX_BOOST_PRESSURE + 0.2f)); // overboost branch
    hal_mock_set_millis((uint32_t)(SOLENOID_UPDATE_TIME + 1));

    Turbo_process(&turbo);

    TEST_ASSERT_EQUAL_INT(0, (int)getGlobalValue(F_PRESSURE_PERCENTAGE));
    TEST_ASSERT_TRUE(getGlobalValue(F_PRESSURE_PERCENTAGE) >= 0.0f);
}

void test_turbo_pressure_correction_is_zero_below_threshold(void) {
    Turbo turbo = {0};

    setGlobalValue(F_INTAKE_TEMP, (float)(MIN_TEMPERATURE_CORRECTION - 1));

    TEST_ASSERT_EQUAL_INT(0, Turbo_correctPressureFactor(&turbo));
}

void test_turbo_pressure_correction_starts_at_one_on_threshold(void) {
    Turbo turbo = {0};

    setGlobalValue(F_INTAKE_TEMP, (float)MIN_TEMPERATURE_CORRECTION);

    TEST_ASSERT_EQUAL_INT(1, Turbo_correctPressureFactor(&turbo));
}

void test_turbo_pressure_correction_steps_every_five_degrees(void) {
    Turbo turbo = {0};

    setGlobalValue(F_INTAKE_TEMP, 40.0f);

    TEST_ASSERT_EQUAL_INT(3, Turbo_correctPressureFactor(&turbo));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_turbo_uses_last_rpm_row_when_rpm_exceeds_limit);
    RUN_TEST(test_turbo_applies_intake_temperature_correction);
    RUN_TEST(test_turbo_overboost_path_never_sets_negative_pressure_percentage);
    RUN_TEST(test_turbo_pressure_correction_is_zero_below_threshold);
    RUN_TEST(test_turbo_pressure_correction_starts_at_one_on_threshold);
    RUN_TEST(test_turbo_pressure_correction_steps_every_five_degrees);
    return UNITY_END();
}
