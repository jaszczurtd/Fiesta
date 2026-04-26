#include "unity.h"

#include "config.h"
#include "hal/impl/.mock/hal_mock.h"

#include <string.h>

extern "C" const char *test_stubs_last_forwarded_serial_line(void);
extern "C" unsigned test_stubs_forwarded_serial_count(void);
extern "C" void test_stubs_reset_forwarded_serial(void);

static const char *sendSerialLine(const char *line) {
    hal_mock_serial_reset();
    hal_mock_serial_inject_rx(line, -1);
    configSessionTick();
    return hal_mock_serial_last_line();
}

static void performHello(void) {
    const char *response = sendSerialLine("HELLO\n");
    TEST_ASSERT_NOT_NULL(response);
    TEST_ASSERT_NOT_NULL(strstr(response, "OK HELLO"));
    TEST_ASSERT_TRUE(configSessionActive());
    TEST_ASSERT_NOT_EQUAL(0u, configSessionId());
}

void setUp(void) {
    hal_mock_set_millis(0);
    configSessionInit();
    ecuParamsInit();
    test_stubs_reset_forwarded_serial();
    hal_mock_serial_reset();
}

void tearDown(void) {}

void test_sc_get_meta_requires_hello_first(void) {
    const char *response = sendSerialLine("SC_GET_META\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_NOT_READY"));
    TEST_ASSERT_NOT_NULL(strstr(response, "HELLO_REQUIRED"));
    TEST_ASSERT_EQUAL_UINT(0u, test_stubs_forwarded_serial_count());
}

void test_sc_get_meta_returns_sc_ok_with_identity_fields(void) {
    performHello();

    const char *response = sendSerialLine("SC_GET_META\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK META"));
    TEST_ASSERT_NOT_NULL(strstr(response, "module=ECU"));
    TEST_ASSERT_NOT_NULL(strstr(response, "proto=1"));
    TEST_ASSERT_NOT_NULL(strstr(response, "session="));
    TEST_ASSERT_NOT_NULL(strstr(response, "fw="));
    TEST_ASSERT_NOT_NULL(strstr(response, "build="));
    TEST_ASSERT_NOT_NULL(strstr(response, "uid="));
}

void test_sc_get_param_list_returns_all_known_ids(void) {
    performHello();

    const char *response = sendSerialLine("SC_GET_PARAM_LIST\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK PARAM_LIST"));
    TEST_ASSERT_NOT_NULL(strstr(response, "fan_coolant_start_c"));
    TEST_ASSERT_NOT_NULL(strstr(response, "fan_coolant_stop_c"));
    TEST_ASSERT_NOT_NULL(strstr(response, "fan_air_start_c"));
    TEST_ASSERT_NOT_NULL(strstr(response, "fan_air_stop_c"));
    TEST_ASSERT_NOT_NULL(strstr(response, "heater_stop_c"));
    TEST_ASSERT_NOT_NULL(strstr(response, "nominal_rpm"));
}

void test_sc_get_param_returns_value_and_bounds(void) {
    performHello();

    const char *response = sendSerialLine("SC_GET_PARAM nominal_rpm\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK PARAM id=nominal_rpm"));
    TEST_ASSERT_NOT_NULL(strstr(response, "value=890"));
    TEST_ASSERT_NOT_NULL(strstr(response, "min=700"));
    TEST_ASSERT_NOT_NULL(strstr(response, "max=1200"));
    TEST_ASSERT_NOT_NULL(strstr(response, "default=890"));
}

void test_sc_get_param_rejects_unknown_parameter(void) {
    performHello();

    const char *response = sendSerialLine("SC_GET_PARAM unknown_param\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_INVALID_PARAM_ID"));
    TEST_ASSERT_NOT_NULL(strstr(response, "id=unknown_param"));
}

void test_sc_get_param_requires_argument(void) {
    performHello();

    const char *response = sendSerialLine("SC_GET_PARAM\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_BAD_REQUEST"));
}

void test_sc_unknown_command_returns_sc_unknown_cmd(void) {
    performHello();

    const char *response = sendSerialLine("SC_DO_SOMETHING\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_UNKNOWN_CMD"));
}

void test_sc_get_values_returns_snapshot(void) {
    performHello();

    const char *response = sendSerialLine("SC_GET_VALUES\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK PARAM_VALUES"));
    TEST_ASSERT_NOT_NULL(strstr(response, "fan_coolant_start_c=102"));
    TEST_ASSERT_NOT_NULL(strstr(response, "fan_coolant_stop_c=95"));
    TEST_ASSERT_NOT_NULL(strstr(response, "fan_air_start_c=55"));
    TEST_ASSERT_NOT_NULL(strstr(response, "fan_air_stop_c=45"));
    TEST_ASSERT_NOT_NULL(strstr(response, "heater_stop_c=80"));
    TEST_ASSERT_NOT_NULL(strstr(response, "nominal_rpm=890"));
}

void test_non_sc_unknown_lines_are_still_forwarded_to_legacy_handler(void) {
    const char *response = sendSerialLine("WHAT\n");
    TEST_ASSERT_EQUAL_STRING("", response);
    TEST_ASSERT_EQUAL_UINT(1u, test_stubs_forwarded_serial_count());
    TEST_ASSERT_EQUAL_STRING("WHAT", test_stubs_last_forwarded_serial_line());
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_sc_get_meta_requires_hello_first);
    RUN_TEST(test_sc_get_meta_returns_sc_ok_with_identity_fields);
    RUN_TEST(test_sc_get_param_list_returns_all_known_ids);
    RUN_TEST(test_sc_get_param_returns_value_and_bounds);
    RUN_TEST(test_sc_get_param_rejects_unknown_parameter);
    RUN_TEST(test_sc_get_param_requires_argument);
    RUN_TEST(test_sc_unknown_command_returns_sc_unknown_cmd);
    RUN_TEST(test_sc_get_values_returns_snapshot);
    RUN_TEST(test_non_sc_unknown_lines_are_still_forwarded_to_legacy_handler);

    return UNITY_END();
}
