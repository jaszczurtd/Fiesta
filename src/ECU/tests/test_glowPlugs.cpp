#include "unity.h"
#include "glowPlugs.h"
#include "sensors.h"
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
    setGlobalValue(F_COOLANT_TEMP, 0.0f);
    setGlobalValue(F_RPM, 0.0f);
    gp.init();
}

void tearDown(void) {}

// ── calculateGlowPlugsTime boundary ──────────────────────────────────────────

void test_glow_no_heating_warm_engine(void) {
    gp.initGlowPlugsTime(50.0f);
    TEST_ASSERT_FALSE(gp.isGlowPlugsHeating());
}

void test_glow_no_heating_at_threshold(void) {
    /* temp == TEMP_COLD_ENGINE: condition is strict < so time = 0 */
    gp.initGlowPlugsTime((float)TEMP_COLD_ENGINE);
    TEST_ASSERT_FALSE(gp.isGlowPlugsHeating());
}

void test_glow_heating_cold_engine(void) {
    gp.initGlowPlugsTime(0.0f);
    TEST_ASSERT_TRUE(gp.isGlowPlugsHeating());
}

void test_glow_heating_very_cold_engine(void) {
    gp.initGlowPlugsTime(TEMP_VERY_LOW);
    TEST_ASSERT_TRUE(gp.isGlowPlugsHeating());
}

void test_glow_heating_just_below_threshold(void) {
    /* 44°C is 1 degree below threshold → small but positive heating time */
    gp.initGlowPlugsTime((float)(TEMP_COLD_ENGINE - 1));
    TEST_ASSERT_TRUE(gp.isGlowPlugsHeating());
}

// ── process() countdown ───────────────────────────────────────────────────────

void test_glow_still_heating_before_expiry(void) {
    /* At 0°C: glowPlugsTime = 60.  Run MAX_GLOW_PLUGS_TIME - 1 ticks. */
    gp.initGlowPlugsTime(0.0f);

    for (int i = 1; i < MAX_GLOW_PLUGS_TIME; i++) {
        hal_mock_set_millis((uint32_t)(i * 1000));
        gp.process();
    }
    TEST_ASSERT_TRUE(gp.isGlowPlugsHeating());
}

void test_glow_process_disables_after_elapsed_time(void) {
    /* At 0°C: glowPlugsTime = 60.  Run MAX_GLOW_PLUGS_TIME + 2 ticks. */
    gp.initGlowPlugsTime(0.0f);

    for (int i = 1; i <= MAX_GLOW_PLUGS_TIME + 2; i++) {
        hal_mock_set_millis((uint32_t)(i * 1000));
        gp.process();
    }
    TEST_ASSERT_FALSE(gp.isGlowPlugsHeating());
}

// ── warmAfterStart flag ───────────────────────────────────────────────────────

void test_glow_warm_after_start_prevents_reheat(void) {
    /*
     * Engine starts warm (no heating), then process() sees warm coolant and
     * sets warmAfterStart = true.  Subsequent cold + RPM conditions must NOT
     * re-trigger heating.
     */
    gp.initGlowPlugsTime(50.0f);
    TEST_ASSERT_FALSE(gp.isGlowPlugsHeating());

    /* First process tick: coolant = 50 > TEMP_COLD_ENGINE → warmAfterStart */
    setGlobalValue(F_COOLANT_TEMP, 50.0f);
    hal_mock_set_millis(1000);
    gp.process();

    /* Now cold coolant + RPM > RPM_MIN — should NOT reheat */
    setGlobalValue(F_COOLANT_TEMP, 0.0f);
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    hal_mock_set_millis(2000);
    gp.process();

    TEST_ASSERT_FALSE(gp.isGlowPlugsHeating());
}

void test_glow_process_triggers_heat_on_cold_rpm(void) {
    /*
     * warmAfterStart starts false.  If process() sees cold coolant AND
     * RPM > RPM_MIN it recalculates heating time and sets warmAfterStart.
     */
    gp.initGlowPlugsTime(50.0f);  /* no initial heating, warmAfterStart=false */

    setGlobalValue(F_COOLANT_TEMP, 0.0f);
    setGlobalValue(F_RPM, (float)(RPM_MIN + 10));
    hal_mock_set_millis(1000);
    gp.process();

    TEST_ASSERT_TRUE(gp.isGlowPlugsHeating());
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(void) {
    /* initSensors() creates mutexes and zeros the global value array.
     * It must be called once before any test that uses setGlobalValue(). */
    initSensors();

    UNITY_BEGIN();

    RUN_TEST(test_glow_no_heating_warm_engine);
    RUN_TEST(test_glow_no_heating_at_threshold);
    RUN_TEST(test_glow_heating_cold_engine);
    RUN_TEST(test_glow_heating_very_cold_engine);
    RUN_TEST(test_glow_heating_just_below_threshold);
    RUN_TEST(test_glow_still_heating_before_expiry);
    RUN_TEST(test_glow_process_disables_after_elapsed_time);
    RUN_TEST(test_glow_warm_after_start_prevents_reheat);
    RUN_TEST(test_glow_process_triggers_heat_on_cold_rpm);

    return UNITY_END();
}
