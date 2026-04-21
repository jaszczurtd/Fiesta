#include "unity.h"
#include "glowPlugs.h"
#include "sensors.h"
#include "hal/hal_eeprom.h"
#include "hal/impl/.mock/hal_mock.h"

/*
 * glowPlugs unit tests.
 *
 * Constants relevant to these tests (from config.h / canDefinitions.h):
 *   TEMP_COLD_ENGINE              = 45   (= TEMP_MINIMUM_FOR_GLOW_PLUGS)
 *   TEMP_VERY_LOW                 = -25
 *   MAX_GLOW_PLUGS_TIME           = SECONDS_IN_MINUTE = 60
 *   RPM_MIN                       = 350
 *
 * calculateGlowPlugsTime(temp):
 *   if temp < 45: time = (int)(60 * (45 - temp) / 45)
 *   else:         time = 0
 *
 * process() decrements glowPlugsTime once per second tick and calls
 * enableGlowPlugs(false) when glowPlugsTime-- hits 0 (post-decrement check).
 * This means it takes MAX_GLOW_PLUGS_TIME + 1 ticks to fully disable.
 */

static glowPlugs gp;

void setUp(void) {
    hal_mock_set_millis(0);
    hal_i2c_init(4, 5, 400000);
    initI2C();
    setGlobalValue(F_COOLANT_TEMP, 0.0f);
    setGlobalValue(F_RPM, 0.0f);
    glowPlugs_init(&gp);
}

void tearDown(void) {}

// ── calculateGlowPlugsTime boundary ──────────────────────────────────────────

void test_glow_no_heating_warm_engine(void) {
    glowPlugs_initGlowPlugsTime(&gp, 50.0f);
    TEST_ASSERT_FALSE(glowPlugs_isGlowPlugsHeating(&gp));
}

void test_glow_no_heating_at_threshold(void) {
    /* temp == TEMP_COLD_ENGINE: condition is strict < so time = 0 */
    glowPlugs_initGlowPlugsTime(&gp, (float)TEMP_COLD_ENGINE);
    TEST_ASSERT_FALSE(glowPlugs_isGlowPlugsHeating(&gp));
}

void test_glow_heating_cold_engine(void) {
    glowPlugs_initGlowPlugsTime(&gp, 0.0f);
    TEST_ASSERT_TRUE(glowPlugs_isGlowPlugsHeating(&gp));
}

void test_glow_heating_very_cold_engine(void) {
    glowPlugs_initGlowPlugsTime(&gp, TEMP_VERY_LOW);
    TEST_ASSERT_TRUE(glowPlugs_isGlowPlugsHeating(&gp));
}

void test_glow_heating_just_below_threshold(void) {
    /* 44°C is 1 degree below threshold → small but positive heating time */
    glowPlugs_initGlowPlugsTime(&gp, (float)(TEMP_COLD_ENGINE - 1));
    TEST_ASSERT_TRUE(glowPlugs_isGlowPlugsHeating(&gp));
}

// ── process() countdown ───────────────────────────────────────────────────────

void test_glow_still_heating_before_expiry(void) {
    /* At 0°C: glowPlugsTime = 60.  Run MAX_GLOW_PLUGS_TIME - 1 ticks. */
    glowPlugs_initGlowPlugsTime(&gp, 0.0f);

    for (int i = 1; i < MAX_GLOW_PLUGS_TIME; i++) {
        hal_mock_set_millis((uint32_t)(i * 1000));
        glowPlugs_process(&gp);
    }
    TEST_ASSERT_TRUE(glowPlugs_isGlowPlugsHeating(&gp));
}

void test_glow_process_disables_after_elapsed_time(void) {
    /* At 0°C: glowPlugsTime = 60.  Run MAX_GLOW_PLUGS_TIME + 2 ticks. */
    glowPlugs_initGlowPlugsTime(&gp, 0.0f);

    for (int i = 1; i <= MAX_GLOW_PLUGS_TIME + 2; i++) {
        hal_mock_set_millis((uint32_t)(i * 1000));
        glowPlugs_process(&gp);
    }
    TEST_ASSERT_FALSE(glowPlugs_isGlowPlugsHeating(&gp));
}

void test_glow_process_does_not_decrement_in_same_second(void) {
    glowPlugs_initGlowPlugsTime(&gp, 0.0f);
    int32_t before = gp.glowPlugsTime;

    hal_mock_set_millis(1000);
    glowPlugs_process(&gp);
    int32_t afterFirstTick = gp.glowPlugsTime;

    glowPlugs_process(&gp);
    TEST_ASSERT_EQUAL_INT32(before - 1, afterFirstTick);
    TEST_ASSERT_EQUAL_INT32(afterFirstTick, gp.glowPlugsTime);
}

void test_glow_process_returns_early_when_not_initialized(void) {
    gp.glowPlugsTime = 5;
    gp.glowPlugsLampTime = 5;
    gp.initialized = false;

    hal_mock_set_millis(1000);
    glowPlugs_process(&gp);

    TEST_ASSERT_EQUAL_INT32(5, gp.glowPlugsTime);
    TEST_ASSERT_EQUAL_INT32(5, gp.glowPlugsLampTime);
}

// ── warmAfterStart flag ───────────────────────────────────────────────────────

void test_glow_warm_after_start_prevents_reheat(void) {
    /*
     * Engine starts warm (no heating), then process() sees warm coolant and
     * sets warmAfterStart = true.  Subsequent cold + RPM conditions must NOT
     * re-trigger heating.
     */
    glowPlugs_initGlowPlugsTime(&gp, 50.0f);
    TEST_ASSERT_FALSE(glowPlugs_isGlowPlugsHeating(&gp));

    /* First process tick: coolant = 50 > TEMP_COLD_ENGINE → warmAfterStart */
    setGlobalValue(F_COOLANT_TEMP, 50.0f);
    hal_mock_set_millis(1000);
    glowPlugs_process(&gp);

    /* Now cold coolant + RPM > RPM_MIN — should NOT reheat */
    setGlobalValue(F_COOLANT_TEMP, 0.0f);
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    hal_mock_set_millis(2000);
    glowPlugs_process(&gp);

    TEST_ASSERT_FALSE(glowPlugs_isGlowPlugsHeating(&gp));
}

void test_glow_process_triggers_heat_on_cold_rpm(void) {
    /*
     * warmAfterStart starts false.  If process() sees cold coolant AND
     * RPM > RPM_MIN it recalculates heating time and sets warmAfterStart.
     */
    glowPlugs_initGlowPlugsTime(&gp, 50.0f);  /* no initial heating, warmAfterStart=false */

    setGlobalValue(F_COOLANT_TEMP, 0.0f);
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    hal_mock_set_millis(1000);
    glowPlugs_process(&gp);

    TEST_ASSERT_TRUE(glowPlugs_isGlowPlugsHeating(&gp));
}

void test_glow_lamp_time_is_max_for_very_low_temperature(void) {
    glowPlugs_initGlowPlugsTime(&gp, TEMP_VERY_LOW);
    TEST_ASSERT_EQUAL_INT32(MAX_LAMP_TIME, gp.glowPlugsLampTime);
}

void test_glow_lamp_time_stays_zero_for_warm_temperature(void) {
    glowPlugs_initGlowPlugsTime(&gp, (float)(TEMP_COLD_ENGINE + 5));
    TEST_ASSERT_EQUAL_INT32(0, gp.glowPlugsLampTime);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(void) {
    /* initSensors() creates mutexes and zeros the global value array.
     * It must be called once before any test that uses setGlobalValue(). */
    initSensors();
    hal_eeprom_init(HAL_EEPROM_RP2040, 512, 0);

    UNITY_BEGIN();

    RUN_TEST(test_glow_no_heating_warm_engine);
    RUN_TEST(test_glow_no_heating_at_threshold);
    RUN_TEST(test_glow_heating_cold_engine);
    RUN_TEST(test_glow_heating_very_cold_engine);
    RUN_TEST(test_glow_heating_just_below_threshold);
    RUN_TEST(test_glow_still_heating_before_expiry);
    RUN_TEST(test_glow_process_disables_after_elapsed_time);
    RUN_TEST(test_glow_process_does_not_decrement_in_same_second);
    RUN_TEST(test_glow_process_returns_early_when_not_initialized);
    RUN_TEST(test_glow_warm_after_start_prevents_reheat);
    RUN_TEST(test_glow_process_triggers_heat_on_cold_rpm);
    RUN_TEST(test_glow_lamp_time_is_max_for_very_low_temperature);
    RUN_TEST(test_glow_lamp_time_stays_zero_for_warm_temperature);

    return UNITY_END();
}
