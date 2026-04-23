#include "unity.h"
#include "hal/hal_pid_controller.h"
#include "hal/hal_soft_timer.h"
#include "hal/hal_serial.h"
#include "hal/hal_system.h"
#include "hal/impl/.mock/hal_mock.h"
#include "config.h"
#include "utils/tools_api.h"
#include <string.h>

static int s_soft_timer_hits = 0;

static void testSoftTimerCallback(void) {
    s_soft_timer_hits++;
}

void setUp(void) {
    hal_mock_set_millis(0);
    s_soft_timer_hits = 0;
}

void tearDown(void) {}

void test_soft_timer_periodic_tick(void) {
    hal_soft_timer_t timer = hal_soft_timer_create();
    TEST_ASSERT_NOT_NULL(timer);

    hal_mock_set_millis(1);
    TEST_ASSERT_TRUE(hal_soft_timer_begin(timer, testSoftTimerCallback, 100u));

    hal_mock_set_millis(50);
    hal_soft_timer_tick(timer);
    TEST_ASSERT_EQUAL_INT(0, s_soft_timer_hits);

    hal_mock_set_millis(101);
    hal_soft_timer_tick(timer);
    TEST_ASSERT_EQUAL_INT(1, s_soft_timer_hits);

    hal_mock_set_millis(150);
    hal_soft_timer_tick(timer);
    TEST_ASSERT_EQUAL_INT(1, s_soft_timer_hits);

    hal_mock_set_millis(201);
    hal_soft_timer_tick(timer);
    TEST_ASSERT_EQUAL_INT(2, s_soft_timer_hits);

    hal_soft_timer_destroy(timer);
}

void test_soft_timer_time_left_and_available(void) {
    hal_soft_timer_t timer = hal_soft_timer_create();
    TEST_ASSERT_NOT_NULL(timer);

    hal_mock_set_millis(1);
    TEST_ASSERT_TRUE(hal_soft_timer_begin(timer, testSoftTimerCallback, 100u));

    hal_mock_set_millis(51);
    TEST_ASSERT_FALSE(hal_soft_timer_available(timer));
    TEST_ASSERT_UINT32_WITHIN(1u, 50u, hal_soft_timer_time_left(timer));

    hal_mock_set_millis(101);
    TEST_ASSERT_TRUE(hal_soft_timer_available(timer));
    TEST_ASSERT_EQUAL_UINT32(0u, hal_soft_timer_time_left(timer));

    hal_soft_timer_destroy(timer);
}

void test_soft_timer_set_interval_restart_and_abort(void) {
    hal_soft_timer_t timer = hal_soft_timer_create();
    TEST_ASSERT_NOT_NULL(timer);

    hal_mock_set_millis(1);
    TEST_ASSERT_TRUE(hal_soft_timer_begin(timer, testSoftTimerCallback, 100u));

    hal_soft_timer_set_interval(timer, 200u);
    hal_soft_timer_restart(timer);

    hal_mock_set_millis(150);
    hal_soft_timer_tick(timer);
    TEST_ASSERT_EQUAL_INT(0, s_soft_timer_hits);

    hal_mock_set_millis(201);
    hal_soft_timer_tick(timer);
    TEST_ASSERT_EQUAL_INT(1, s_soft_timer_hits);

    hal_soft_timer_abort(timer);
    hal_mock_set_millis(500);
    hal_soft_timer_tick(timer);
    TEST_ASSERT_EQUAL_INT(1, s_soft_timer_hits);

    hal_soft_timer_destroy(timer);
}

void test_soft_timer_null_safety(void) {
    TEST_ASSERT_FALSE(hal_soft_timer_begin(NULL, testSoftTimerCallback, 10u));
    TEST_ASSERT_FALSE(hal_soft_timer_available(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, hal_soft_timer_time_left(NULL));

    hal_soft_timer_restart(NULL);
    hal_soft_timer_set_interval(NULL, 10u);
    hal_soft_timer_tick(NULL);
    hal_soft_timer_abort(NULL);
    hal_soft_timer_destroy(NULL);
}

void test_pid_set_get_roundtrip(void) {
    hal_pid_controller_t pid = hal_pid_controller_create();
    TEST_ASSERT_NOT_NULL(pid);

    hal_pid_controller_set_kp(pid, 1.5f);
    hal_pid_controller_set_ki(pid, 0.4f);
    hal_pid_controller_set_kd(pid, 0.2f);
    hal_pid_controller_set_tf(pid, 0.03f);
    hal_pid_controller_set_max_integral(pid, 123.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, hal_pid_controller_get_kp(pid));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.4f, hal_pid_controller_get_ki(pid));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.2f, hal_pid_controller_get_kd(pid));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.03f, hal_pid_controller_get_tf(pid));

    hal_pid_controller_destroy(pid);
}

void test_pid_update_and_output_limits(void) {
    hal_pid_controller_t pid = hal_pid_controller_create();
    TEST_ASSERT_NOT_NULL(pid);

    hal_pid_controller_set_kp(pid, 2.0f);
    hal_pid_controller_set_ki(pid, 0.0f);
    hal_pid_controller_set_kd(pid, 0.0f);
    hal_pid_controller_set_max_integral(pid, 100.0f);
    hal_pid_controller_set_output_limits(pid, -5.0f, 5.0f);

    hal_mock_set_millis(100);
    hal_pid_controller_update_time(pid, 100.0f);
    float out = hal_pid_controller_update(pid, 10.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, out);

    hal_pid_controller_destroy(pid);
}

void test_pid_direction_backward_inverts_response(void) {
    hal_pid_controller_t pid = hal_pid_controller_create();
    TEST_ASSERT_NOT_NULL(pid);

    hal_pid_controller_set_kp(pid, 2.0f);
    hal_pid_controller_set_ki(pid, 0.0f);
    hal_pid_controller_set_kd(pid, 0.0f);
    hal_pid_controller_set_max_integral(pid, 100.0f);

    hal_mock_set_millis(100);
    hal_pid_controller_update_time(pid, 100.0f);
    float forward = hal_pid_controller_update(pid, 3.0f);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, forward);

    hal_pid_controller_set_direction(pid, HAL_PID_DIRECTION_BACKWARD);
    hal_mock_set_millis(200);
    hal_pid_controller_update_time(pid, 100.0f);
    float backward = hal_pid_controller_update(pid, 3.0f);
    TEST_ASSERT_LESS_THAN_FLOAT(0.0f, backward);

    hal_pid_controller_destroy(pid);
}

void test_pid_stability_and_oscillation_helpers(void) {
    hal_pid_controller_t pid = hal_pid_controller_create();
    TEST_ASSERT_NOT_NULL(pid);

    bool stable1 = hal_pid_controller_is_error_stable(pid, 0.1f, 0.5f, 3);
    bool stable2 = hal_pid_controller_is_error_stable(pid, 0.1f, 0.5f, 3);
    bool stable3 = hal_pid_controller_is_error_stable(pid, 0.1f, 0.5f, 3);
    TEST_ASSERT_FALSE(stable1);
    TEST_ASSERT_FALSE(stable2);
    TEST_ASSERT_TRUE(stable3);

    bool oscillating = false;
    oscillating = hal_pid_controller_is_oscillating(pid, 1.0f, 6);
    oscillating = hal_pid_controller_is_oscillating(pid, -1.0f, 6);
    oscillating = hal_pid_controller_is_oscillating(pid, 1.0f, 6);
    oscillating = hal_pid_controller_is_oscillating(pid, -1.0f, 6);
    oscillating = hal_pid_controller_is_oscillating(pid, 1.0f, 6);
    oscillating = hal_pid_controller_is_oscillating(pid, -1.0f, 6);
    TEST_ASSERT_TRUE(oscillating);

    hal_pid_controller_destroy(pid);
}

void test_pid_null_safety(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, hal_pid_controller_get_kp(NULL));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, hal_pid_controller_get_ki(NULL));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, hal_pid_controller_get_kd(NULL));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, hal_pid_controller_get_tf(NULL));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, hal_pid_controller_update(NULL, 1.0f));
    TEST_ASSERT_FALSE(hal_pid_controller_is_error_stable(NULL, 0.0f, 1.0f, 1));
    TEST_ASSERT_FALSE(hal_pid_controller_is_oscillating(NULL, 0.0f, 4));

    hal_pid_controller_set_kp(NULL, 1.0f);
    hal_pid_controller_set_ki(NULL, 1.0f);
    hal_pid_controller_set_kd(NULL, 1.0f);
    hal_pid_controller_set_tf(NULL, 0.1f);
    hal_pid_controller_set_max_integral(NULL, 1.0f);
    hal_pid_controller_update_time(NULL, 10.0f);
    hal_pid_controller_set_output_limits(NULL, -1.0f, 1.0f);
    hal_pid_controller_set_direction(NULL, HAL_PID_DIRECTION_FORWARD);
    hal_pid_controller_reset(NULL);
    hal_pid_controller_destroy(NULL);
}

// ── hal_serial_available / hal_serial_read tests ─────────────────────────────

void test_serial_read_empty_returns_minus1(void) {
    hal_mock_serial_reset();
    TEST_ASSERT_EQUAL_INT(0, hal_serial_available());
    TEST_ASSERT_EQUAL_INT(-1, hal_serial_read());
}

void test_serial_inject_and_read_back(void) {
    hal_mock_serial_reset();
    hal_mock_serial_inject_rx("P0.5\n", 5);
    TEST_ASSERT_EQUAL_INT(5, hal_serial_available());

    TEST_ASSERT_EQUAL_INT('P', hal_serial_read());
    TEST_ASSERT_EQUAL_INT(4, hal_serial_available());
    TEST_ASSERT_EQUAL_INT('0', hal_serial_read());
    TEST_ASSERT_EQUAL_INT('.', hal_serial_read());
    TEST_ASSERT_EQUAL_INT('5', hal_serial_read());
    TEST_ASSERT_EQUAL_INT('\n', hal_serial_read());

    TEST_ASSERT_EQUAL_INT(0, hal_serial_available());
    TEST_ASSERT_EQUAL_INT(-1, hal_serial_read());
}

void test_serial_reset_clears_rx_buffer(void) {
    hal_mock_serial_inject_rx("ABC", 3);
    TEST_ASSERT_EQUAL_INT(3, hal_serial_available());

    hal_mock_serial_reset();
    TEST_ASSERT_EQUAL_INT(0, hal_serial_available());
    TEST_ASSERT_EQUAL_INT(-1, hal_serial_read());
}

void test_serial_inject_auto_length(void) {
    hal_mock_serial_reset();
    hal_mock_serial_inject_rx("?\n", -1);
    TEST_ASSERT_EQUAL_INT(2, hal_serial_available());
    TEST_ASSERT_EQUAL_INT('?', hal_serial_read());
    TEST_ASSERT_EQUAL_INT('\n', hal_serial_read());
    TEST_ASSERT_EQUAL_INT(-1, hal_serial_read());
}

// ── hal_enter_bootloader tests ───────────────────────────────────────────────

void test_enter_bootloader_sets_mock_flag(void) {
    hal_mock_bootloader_reset_flag();
    TEST_ASSERT_FALSE(hal_mock_bootloader_was_requested());

    hal_enter_bootloader();

    TEST_ASSERT_TRUE(hal_mock_bootloader_was_requested());
}

// ── ECU config session HELLO tests ───────────────────────────────────────────

void test_ecu_config_session_hello_path(void) {
    configSessionInit();
    hal_mock_serial_reset();

    hal_mock_serial_inject_rx("HELLO\n", -1);
    configSessionTick();

    TEST_ASSERT_TRUE(configSessionActive());
    TEST_ASSERT_NOT_EQUAL(0u, configSessionId());
    const char *line = hal_mock_serial_last_line();
    TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "module=ECU"));
    TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "fw="));
    TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "build="));
    TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "uid="));
}

void test_ecu_config_session_unknown_command_returns_error(void) {
    configSessionInit();
    hal_mock_serial_reset();

    hal_mock_serial_inject_rx("WHAT\n", -1);
    configSessionTick();

    TEST_ASSERT_FALSE(configSessionActive());
    TEST_ASSERT_EQUAL_UINT32(0u, configSessionId());
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN", hal_mock_serial_last_line());
}

// ── float_to_u32 / u32_to_float tests ───────────────────────────────────────

void test_float_to_u32_roundtrip(void) {
    float values[] = {0.0f, 1.0f, -1.0f, 0.42f, 3.14159f, 1e10f, 1e-10f};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        uint32_t u = float_to_u32(values[i]);
        float back = u32_to_float(u);
        TEST_ASSERT_EQUAL_FLOAT(values[i], back);
    }
}

void test_float_to_u32_known_pattern(void) {
    // IEEE 754: 1.0f = 0x3F800000
    TEST_ASSERT_EQUAL_HEX32(0x3F800000u, float_to_u32(1.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, u32_to_float(0x3F800000u));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_soft_timer_periodic_tick);
    RUN_TEST(test_soft_timer_time_left_and_available);
    RUN_TEST(test_soft_timer_set_interval_restart_and_abort);
    RUN_TEST(test_soft_timer_null_safety);

    RUN_TEST(test_pid_set_get_roundtrip);
    RUN_TEST(test_pid_update_and_output_limits);
    RUN_TEST(test_pid_direction_backward_inverts_response);
    RUN_TEST(test_pid_stability_and_oscillation_helpers);
    RUN_TEST(test_pid_null_safety);

    RUN_TEST(test_serial_read_empty_returns_minus1);
    RUN_TEST(test_serial_inject_and_read_back);
    RUN_TEST(test_serial_reset_clears_rx_buffer);
    RUN_TEST(test_serial_inject_auto_length);
    RUN_TEST(test_enter_bootloader_sets_mock_flag);
    RUN_TEST(test_ecu_config_session_hello_path);
    RUN_TEST(test_ecu_config_session_unknown_command_returns_error);

    RUN_TEST(test_float_to_u32_roundtrip);
    RUN_TEST(test_float_to_u32_known_pattern);

    return UNITY_END();
}
