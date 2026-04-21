#include "unity.h"
#include "heatedWindshield.h"
#include "sensors.h"
#include "hal/hal_eeprom.h"
#include "hal/impl/.mock/hal_mock.h"

/*
 * heatedWindshield unit tests.
 *
 * Key behavior:
 * - Button is active-low on HEATED_WINDOWS_PIN.
 * - waitingForUnpress prevents repeated toggles while button is held.
 * - volts < MINIMUM_VOLTS_AMOUNT blocks enabling and forces disable when active.
 * - Runtime auto-disable uses second-based countdown (post-decrement check).
 */

static heatedWindshields hw;

static void setButtonReleased(void) {
    hal_mock_gpio_inject_level(HEATED_WINDOWS_PIN, true);
}

static void setButtonPressed(void) {
    hal_mock_gpio_inject_level(HEATED_WINDOWS_PIN, false);
}

static void enableHeatedWindowByButtonSequence(void) {
    setButtonPressed();
    heatedWindshields_process(&hw);

    setButtonReleased();
    heatedWindshields_process(&hw); /* clears waitingForUnpress */
    heatedWindshields_process(&hw); /* applies output state update path */
}

void setUp(void) {
    hal_mock_set_millis(0);
    hal_i2c_init(4, 5, 400000);
    initI2C();

    heatedWindshields_init(&hw);
    setButtonReleased();
    setGlobalValue(F_VOLTS, 14.0f);
}

void tearDown(void) {}

void test_init_sets_defaults_and_input_mode(void) {
    TEST_ASSERT_FALSE(hw.heatedWindowEnabled);
    TEST_ASSERT_FALSE(hw.lastHeatedWindowEnabled);
    TEST_ASSERT_FALSE(hw.waitingForUnpress);
    TEST_ASSERT_EQUAL_INT32(0, hw.heatedWindowsOverallTimer);
    TEST_ASSERT_EQUAL_UINT32(HAL_GPIO_INPUT_PULLUP, hal_mock_gpio_get_mode(HEATED_WINDOWS_PIN));
}

void test_button_press_enables_heating_when_voltage_ok(void) {
    setButtonPressed();
    heatedWindshields_process(&hw);

    TEST_ASSERT_TRUE(hw.heatedWindowEnabled);
    TEST_ASSERT_TRUE(hw.waitingForUnpress);
    TEST_ASSERT_EQUAL_INT32(HEATED_WINDOWS_TIME * 60, hw.heatedWindowsOverallTimer);
}

void test_waiting_for_unpress_blocks_retrigger_when_button_held(void) {
    setButtonPressed();
    heatedWindshields_process(&hw);
    TEST_ASSERT_TRUE(hw.heatedWindowEnabled);
    TEST_ASSERT_TRUE(hw.waitingForUnpress);

    heatedWindshields_process(&hw);
    TEST_ASSERT_TRUE(hw.heatedWindowEnabled);
    TEST_ASSERT_TRUE(hw.waitingForUnpress);
}

void test_release_clears_waiting_for_unpress(void) {
    setButtonPressed();
    heatedWindshields_process(&hw);
    TEST_ASSERT_TRUE(hw.waitingForUnpress);

    setButtonReleased();
    heatedWindshields_process(&hw);
    TEST_ASSERT_FALSE(hw.waitingForUnpress);
}

void test_low_voltage_blocks_enable(void) {
    setGlobalValue(F_VOLTS, MINIMUM_VOLTS_AMOUNT - 0.1f);
    setButtonPressed();
    heatedWindshields_process(&hw);

    TEST_ASSERT_FALSE(hw.heatedWindowEnabled);
    TEST_ASSERT_TRUE(hw.waitingForUnpress);
    TEST_ASSERT_EQUAL_INT32(0, hw.heatedWindowsOverallTimer);
}

void test_low_voltage_forces_disable_when_active(void) {
    enableHeatedWindowByButtonSequence();
    TEST_ASSERT_TRUE(hw.heatedWindowEnabled);

    setGlobalValue(F_VOLTS, MINIMUM_VOLTS_AMOUNT - 0.1f);
    heatedWindshields_process(&hw);
    TEST_ASSERT_FALSE(hw.heatedWindowEnabled);
    TEST_ASSERT_EQUAL_INT32(0, hw.heatedWindowsOverallTimer);
}

void test_second_press_disables_when_active(void) {
    enableHeatedWindowByButtonSequence();
    TEST_ASSERT_TRUE(hw.heatedWindowEnabled);

    setButtonPressed();
    heatedWindshields_process(&hw);
    TEST_ASSERT_FALSE(hw.heatedWindowEnabled);
    TEST_ASSERT_TRUE(hw.waitingForUnpress);
}

void test_timer_expiry_disables_heating(void) {
    enableHeatedWindowByButtonSequence();
    TEST_ASSERT_TRUE(hw.heatedWindowEnabled);

    setButtonReleased();
    for(int i = 1; i <= (HEATED_WINDOWS_TIME * 60) + 2; i++) {
        hal_mock_set_millis((uint32_t)(i * 1000));
        heatedWindshields_process(&hw);
    }

    TEST_ASSERT_FALSE(hw.heatedWindowEnabled);
}

int main(void) {
    initSensors();
    hal_eeprom_init(HAL_EEPROM_RP2040, 512, 0);

    UNITY_BEGIN();

    RUN_TEST(test_init_sets_defaults_and_input_mode);
    RUN_TEST(test_button_press_enables_heating_when_voltage_ok);
    RUN_TEST(test_waiting_for_unpress_blocks_retrigger_when_button_held);
    RUN_TEST(test_release_clears_waiting_for_unpress);
    RUN_TEST(test_low_voltage_blocks_enable);
    RUN_TEST(test_low_voltage_forces_disable_when_active);
    RUN_TEST(test_second_press_disables_when_active);
    RUN_TEST(test_timer_expiry_disables_heating);

    return UNITY_END();
}

