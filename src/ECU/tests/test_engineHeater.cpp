#include "unity.h"
#include "engineHeater.h"
#include "engineFan.h"
#include "glowPlugs.h"
#include "sensors.h"
#include "hal/hal_eeprom.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

/*
 * engineHeater unit tests.
 *
 * Relevant constants (config.h / canDefinitions.h):
 *   TEMP_HEATER_STOP       =  80   heater off above this coolant temp
 *   MINIMUM_VOLTS_AMOUNT   = 13.0  heater off below this voltage
 *   RPM_MIN                = 350   heater off when engine stopped
 *
 * Heater Hi/Lo selection when all conditions allow heating:
 *   coolant <= (int)(80 / 1.5) = 53  → both Lo and Hi enabled
 *   coolant in (53, 80]             → Lo only
 *
 * The heater state is not exposed via a public getter.  We observe it
 * through engineHeater::showDebug() which produces:
 *   "heaterStatus: loEnabled:<0|1> hiEnabled:<0|1>"
 * and capture it with hal_mock_deb_last_line().
 */

/* Helper: run process() then showDebug(), return true if lo heater is on */
static bool heaterLoOn(engineHeater &h) {
    h.process();
    h.showDebug();
    return strstr(hal_mock_deb_last_line(), "loEnabled:1") != NULL;
}

/* Helper: return true if hi heater is on (call after heaterLoOn or showDebug) */
static bool heaterHiOn(engineHeater &h) {
    h.process();
    h.showDebug();
    return strstr(hal_mock_deb_last_line(), "hiEnabled:1") != NULL;
}

static engineHeater eheater;

void setUp(void) {
    hal_mock_set_millis(0);
    /* Default: conditions that allow the heater to run */
    setGlobalValue(F_COOLANT_TEMP, 30.0f);        /* cold, below stop temp */
    setGlobalValue(F_VOLTS, 14.0f);               /* good voltage */
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10)); /* engine running */

    /* Reset singleton states so fan and glow plugs start disabled */
    getFanInstance()->init();
    getGlowPlugsInstance()->init();
    eheater.init();
}

void tearDown(void) {}

// ── Off-conditions ────────────────────────────────────────────────────────────

void test_heater_off_above_stop_temp(void) {
    setGlobalValue(F_COOLANT_TEMP, (float)(TEMP_HEATER_STOP + 1));
    TEST_ASSERT_FALSE(heaterLoOn(eheater));
}

void test_heater_lo_on_at_stop_temp_boundary(void) {
    /* condition is strict >, so exactly TEMP_HEATER_STOP still allows Lo */
    setGlobalValue(F_COOLANT_TEMP, (float)TEMP_HEATER_STOP);
    eheater.process();
    eheater.showDebug();
    const char *dbg = hal_mock_deb_last_line();
    TEST_ASSERT_NOT_NULL(strstr(dbg, "loEnabled:1"));
    TEST_ASSERT_NOT_NULL(strstr(dbg, "hiEnabled:0"));
}

void test_heater_off_low_voltage(void) {
    setGlobalValue(F_VOLTS, MINIMUM_VOLTS_AMOUNT - 0.5f);
    TEST_ASSERT_FALSE(heaterLoOn(eheater));
}

void test_heater_off_engine_stopped(void) {
    setGlobalValue(F_RPM, 0.0f);
    TEST_ASSERT_FALSE(heaterLoOn(eheater));
}

void test_heater_on_rpm_at_min_boundary(void) {
    /* strict < check: RPM == RPM_MIN still allows heating */
    setGlobalValue(F_RPM, (float)RPM_MIN);
    TEST_ASSERT_TRUE(heaterLoOn(eheater));
}

void test_heater_off_rpm_below_min_boundary(void) {
    setGlobalValue(F_RPM, (float)(RPM_MIN - 1));
    TEST_ASSERT_FALSE(heaterLoOn(eheater));
}

void test_heater_off_when_fan_is_on(void) {
    /*
     * Enable fan via high intake-air temp (air path avoids crossing
     * TEMP_FAN_START 102 which would exceed TEMP_HEATER_STOP 80 on coolant).
     */
    setGlobalValue(F_INTAKE_TEMP, (float)(AIR_TEMP_FAN_START + 1));
    setGlobalValue(F_COOLANT_TEMP, 30.0f);  /* valid, below FAN_START */
    getFanInstance()->process();            /* turns fan on by air reason */
    TEST_ASSERT_TRUE(getFanInstance()->isFanEnabled());

    TEST_ASSERT_FALSE(heaterLoOn(eheater));
}

void test_heater_off_when_glow_plugs_active(void) {
    getGlowPlugsInstance()->initGlowPlugsTime(-10.0f);  /* starts heating */
    TEST_ASSERT_TRUE(getGlowPlugsInstance()->isGlowPlugsHeating());

    TEST_ASSERT_FALSE(heaterLoOn(eheater));
}

// ── On-conditions and Hi/Lo selection ────────────────────────────────────────

void test_heater_lo_on_normal_cold_conditions(void) {
    /* 30°C: <= 53 → both lo and hi should be on */
    setGlobalValue(F_COOLANT_TEMP, 30.0f);
    TEST_ASSERT_TRUE(heaterLoOn(eheater));
}

void test_heater_hi_on_very_cold_coolant(void) {
    /* coolant <= (int)(80/1.5) = 53 → hi also enabled */
    setGlobalValue(F_COOLANT_TEMP, 30.0f);
    TEST_ASSERT_TRUE(heaterHiOn(eheater));
}

void test_heater_lo_only_when_moderately_cold(void) {
    /*
     * 60°C > 53 but <= 80 → lo enabled, hi disabled.
     * We need two separate calls because heaterLoOn / heaterHiOn each
     * call process() — call process() once and then check the debug line.
     */
    setGlobalValue(F_COOLANT_TEMP, 60.0f);
    eheater.process();
    eheater.showDebug();
    const char *dbg = hal_mock_deb_last_line();
    TEST_ASSERT_NOT_NULL(strstr(dbg, "loEnabled:1"));
    TEST_ASSERT_NOT_NULL(strstr(dbg, "hiEnabled:0"));
}

void test_heater_hi_boundary_exactly_at_split(void) {
    /*
     * (int)(80 / 1.5f) = 53.  At coolant == 53: condition is <= 53 → hi on.
     * At coolant == 54: condition fails → hi off.
     */
    setGlobalValue(F_COOLANT_TEMP, 53.0f);
    eheater.process();
    eheater.showDebug();
    TEST_ASSERT_NOT_NULL(strstr(hal_mock_deb_last_line(), "hiEnabled:1"));

    eheater.init();  /* reset lastHeater flags so next process() writes again */
    setGlobalValue(F_COOLANT_TEMP, 54.0f);
    eheater.process();
    eheater.showDebug();
    TEST_ASSERT_NOT_NULL(strstr(hal_mock_deb_last_line(), "hiEnabled:0"));
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(void) {
    initSensors();
    hal_eeprom_init(HAL_EEPROM_RP2040, 512, 0);

    UNITY_BEGIN();

    RUN_TEST(test_heater_off_above_stop_temp);
    RUN_TEST(test_heater_lo_on_at_stop_temp_boundary);
    RUN_TEST(test_heater_off_low_voltage);
    RUN_TEST(test_heater_off_engine_stopped);
    RUN_TEST(test_heater_on_rpm_at_min_boundary);
    RUN_TEST(test_heater_off_rpm_below_min_boundary);
    RUN_TEST(test_heater_off_when_fan_is_on);
    RUN_TEST(test_heater_off_when_glow_plugs_active);
    RUN_TEST(test_heater_lo_on_normal_cold_conditions);
    RUN_TEST(test_heater_hi_on_very_cold_coolant);
    RUN_TEST(test_heater_lo_only_when_moderately_cold);
    RUN_TEST(test_heater_hi_boundary_exactly_at_split);

    return UNITY_END();
}
