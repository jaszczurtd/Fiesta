#include "unity.h"
#include "engineFan.h"
#include "sensors.h"
#include "hal/hal_eeprom.h"
#include "hal/impl/.mock/hal_mock.h"

/*
 * engineFan unit tests.
 *
 * Relevant thresholds (config.h / canDefinitions.h):
 *   RPM_MIN            = 350
 *   TEMP_LOWEST        = -70   (invalid sensor sentinel)
 *   TEMP_FAN_START     = 102
 *   TEMP_FAN_STOP      =  95
 *   AIR_TEMP_FAN_START =  55
 *   AIR_TEMP_FAN_STOP  =  45
 *
 * Fan logic summary:
 *   - rpm <= RPM_MIN           → fan always off (engine stopped)
 *   - coolant <= TEMP_LOWEST   → fan forced on (sensor failure)
 *   - coolant > TEMP_FAN_START → start coolant reason
 *   - coolant <= TEMP_FAN_STOP → stop coolant reason (hysteresis)
 *   - air > AIR_TEMP_FAN_START → start air reason
 *   - air <= AIR_TEMP_FAN_STOP → stop air reason (hysteresis)
 */

static engineFan efan;

void setUp(void) {
    hal_mock_set_millis(0);
    hal_i2c_init(4, 5, 400000);
    initI2C();
    setGlobalValue(F_COOLANT_TEMP, 0.0f);
    setGlobalValue(F_RPM, 0.0f);
    setGlobalValue(F_INTAKE_TEMP, 0.0f);
    engineFan_init(&efan);
}

void tearDown(void) {}

// ── RPM gate ──────────────────────────────────────────────────────────────────

void test_fan_off_when_engine_stopped(void) {
    setGlobalValue(F_RPM, 0.0f);
    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_START + 5));
    engineFan_process(&efan);
    TEST_ASSERT_FALSE(engineFan_isFanEnabled(&efan));
}

void test_fan_off_at_rpm_min_boundary(void) {
    /* RPM == RPM_MIN: condition is strict > so fan must remain off */
    setGlobalValue(F_RPM, (float)RPM_MIN);
    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_START + 5));
    engineFan_process(&efan);
    TEST_ASSERT_FALSE(engineFan_isFanEnabled(&efan));
}

void test_fan_disabled_when_rpm_drops(void) {
    /* Turn on fan, then drop RPM → must turn off */
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_START + 5));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));

    setGlobalValue(F_RPM, 0.0f);
    engineFan_process(&efan);
    TEST_ASSERT_FALSE(engineFan_isFanEnabled(&efan));
}

// ── Coolant temperature reason ────────────────────────────────────────────────

void test_fan_off_below_coolant_start_threshold(void) {
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_START - 1));
    engineFan_process(&efan);
    TEST_ASSERT_FALSE(engineFan_isFanEnabled(&efan));
}

void test_fan_on_by_coolant_overheating(void) {
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_START + 1));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));
}

void test_fan_off_after_coolant_cools_down(void) {
    /* Enable via coolant, then cool below stop threshold */
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_START + 1));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));

    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_STOP - 1));
    engineFan_process(&efan);
    TEST_ASSERT_FALSE(engineFan_isFanEnabled(&efan));
}

void test_fan_off_at_coolant_stop_boundary(void) {
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_START + 1));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));

    setGlobalValue(F_COOLANT_TEMP, (float)TEMP_FAN_STOP);
    engineFan_process(&efan);
    TEST_ASSERT_FALSE(engineFan_isFanEnabled(&efan));
}

void test_fan_hysteresis_coolant_stays_on_between_thresholds(void) {
    /* Enable, then set coolant between stop and start → must stay on */
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_START + 1));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));

    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_STOP + 1));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));
}

// ── Air temperature reason ────────────────────────────────────────────────────

void test_fan_on_by_high_intake_air(void) {
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, 30.0f);  /* below coolant start */
    setGlobalValue(F_INTAKE_TEMP, (float)(AIR_TEMP_FAN_START + 1));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));
}

void test_fan_off_after_intake_air_cools(void) {
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, 30.0f);
    setGlobalValue(F_INTAKE_TEMP, (float)(AIR_TEMP_FAN_START + 1));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));

    setGlobalValue(F_INTAKE_TEMP, (float)(AIR_TEMP_FAN_STOP - 1));
    engineFan_process(&efan);
    TEST_ASSERT_FALSE(engineFan_isFanEnabled(&efan));
}

void test_fan_off_at_air_stop_boundary(void) {
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, 30.0f);
    setGlobalValue(F_INTAKE_TEMP, (float)(AIR_TEMP_FAN_START + 1));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));

    setGlobalValue(F_INTAKE_TEMP, (float)AIR_TEMP_FAN_STOP);
    engineFan_process(&efan);
    TEST_ASSERT_FALSE(engineFan_isFanEnabled(&efan));
}

void test_fan_hysteresis_air_stays_on_between_thresholds(void) {
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, 30.0f);
    setGlobalValue(F_INTAKE_TEMP, (float)(AIR_TEMP_FAN_START + 1));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));

    setGlobalValue(F_INTAKE_TEMP, (float)(AIR_TEMP_FAN_STOP + 1));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));
}

// ── Sensor failure ────────────────────────────────────────────────────────────

void test_fan_forced_on_with_sensor_fault(void) {
    /* coolant == TEMP_LOWEST signals a disconnected sensor → force fan on */
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, (float)TEMP_LOWEST);
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));
}

void test_fan_not_forced_on_just_above_lowest(void) {
    /* One degree above sentinel is still cold but a valid reading → no force */
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_LOWEST + 1));
    engineFan_process(&efan);
    TEST_ASSERT_FALSE(engineFan_isFanEnabled(&efan));
}

void test_fan_stays_on_when_one_reason_remains_active(void) {
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_START + 1));
    setGlobalValue(F_INTAKE_TEMP, (float)(AIR_TEMP_FAN_START + 1));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));

    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_STOP - 1));
    setGlobalValue(F_INTAKE_TEMP, (float)(AIR_TEMP_FAN_STOP + 1));
    engineFan_process(&efan);
    TEST_ASSERT_TRUE(engineFan_isFanEnabled(&efan));
}

void test_fan_writes_state_to_global_value(void) {
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_START + 1));
    engineFan_process(&efan);
    TEST_ASSERT_GREATER_THAN(0, (int32_t)getGlobalValue(F_FAN_ENABLED));

    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_FAN_STOP - 1));
    setGlobalValue(F_INTAKE_TEMP, 0.0f);
    engineFan_process(&efan);
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)getGlobalValue(F_FAN_ENABLED));
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(void) {
    initSensors();
    hal_eeprom_init(HAL_EEPROM_RP2040, 512, 0);

    UNITY_BEGIN();

    RUN_TEST(test_fan_off_when_engine_stopped);
    RUN_TEST(test_fan_off_at_rpm_min_boundary);
    RUN_TEST(test_fan_disabled_when_rpm_drops);
    RUN_TEST(test_fan_off_below_coolant_start_threshold);
    RUN_TEST(test_fan_on_by_coolant_overheating);
    RUN_TEST(test_fan_off_after_coolant_cools_down);
    RUN_TEST(test_fan_off_at_coolant_stop_boundary);
    RUN_TEST(test_fan_hysteresis_coolant_stays_on_between_thresholds);
    RUN_TEST(test_fan_on_by_high_intake_air);
    RUN_TEST(test_fan_off_after_intake_air_cools);
    RUN_TEST(test_fan_off_at_air_stop_boundary);
    RUN_TEST(test_fan_hysteresis_air_stays_on_between_thresholds);
    RUN_TEST(test_fan_forced_on_with_sensor_fault);
    RUN_TEST(test_fan_not_forced_on_just_above_lowest);
    RUN_TEST(test_fan_stays_on_when_one_reason_remains_active);
    RUN_TEST(test_fan_writes_state_to_global_value);

    return UNITY_END();
}
