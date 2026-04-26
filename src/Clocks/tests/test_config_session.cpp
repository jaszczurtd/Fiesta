#include "unity.h"
#include "config.h"
#include "hal/impl/.mock/hal_mock.h"
#include <string.h>

static const char *sendSerialLine(const char *line) {
  hal_mock_serial_reset();
  hal_mock_serial_inject_rx(line, -1);
  configSessionTick();
  return hal_mock_serial_last_line();
}

static void performHello(void) {
  const char *response = sendSerialLine("HELLO\n");
  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(response, "OK HELLO"));
  TEST_ASSERT_TRUE(configSessionActive());
  TEST_ASSERT_NOT_EQUAL(0u, configSessionId());
}

void setUp(void) {
  configSessionInit();
  hal_mock_serial_reset();
  hal_mock_set_millis(0);
}

void tearDown(void) {}

void test_clocks_hello_activates_session_and_reports_module(void) {
  hal_mock_serial_inject_rx("HELLO\n", -1);
  configSessionTick();

  TEST_ASSERT_TRUE(configSessionActive());
  TEST_ASSERT_NOT_EQUAL(0u, configSessionId());

  const char *line = hal_mock_serial_last_line();
  TEST_ASSERT_NOT_NULL(line);
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "OK HELLO"));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "module=" MODULE_NAME));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "proto=1"));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "fw=" FW_VERSION));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "build="));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "uid="));
}

void test_clocks_unknown_command_returns_error(void) {
  const char *response = sendSerialLine("WHAT\n");

  TEST_ASSERT_FALSE(configSessionActive());
  TEST_ASSERT_EQUAL_UINT32(0u, configSessionId());
  TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN", response);
}

void test_clocks_sc_get_meta_requires_hello_first(void) {
  const char *response = sendSerialLine("SC_GET_META\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "SC_NOT_READY"));
  TEST_ASSERT_NOT_NULL(strstr(response, "HELLO_REQUIRED"));
}

void test_clocks_sc_get_meta_returns_identity_fields(void) {
  performHello();

  const char *response = sendSerialLine("SC_GET_META\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK META"));
  TEST_ASSERT_NOT_NULL(strstr(response, "module=" MODULE_NAME));
  TEST_ASSERT_NOT_NULL(strstr(response, "proto=1"));
  TEST_ASSERT_NOT_NULL(strstr(response, "session="));
  TEST_ASSERT_NOT_NULL(strstr(response, "fw=" FW_VERSION));
  TEST_ASSERT_NOT_NULL(strstr(response, "build="));
  TEST_ASSERT_NOT_NULL(strstr(response, "uid="));
}

void test_clocks_sc_get_param_list_returns_empty_list_baseline(void) {
  performHello();

  const char *response = sendSerialLine("SC_GET_PARAM_LIST\n");
  TEST_ASSERT_EQUAL_STRING("SC_OK PARAM_LIST", response);
}

void test_clocks_sc_get_values_returns_empty_snapshot_baseline(void) {
  performHello();

  const char *response = sendSerialLine("SC_GET_VALUES\n");
  TEST_ASSERT_EQUAL_STRING("SC_OK PARAM_VALUES", response);
}

void test_clocks_sc_get_param_returns_invalid_param(void) {
  performHello();

  const char *response = sendSerialLine("SC_GET_PARAM nominal_rpm\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "SC_INVALID_PARAM_ID"));
  TEST_ASSERT_NOT_NULL(strstr(response, "id=nominal_rpm"));
}

void test_clocks_sc_unknown_command_returns_sc_unknown_cmd(void) {
  performHello();

  const char *response = sendSerialLine("SC_DO_SOMETHING\n");
  TEST_ASSERT_EQUAL_STRING("SC_UNKNOWN_CMD", response);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_clocks_hello_activates_session_and_reports_module);
  RUN_TEST(test_clocks_unknown_command_returns_error);
  RUN_TEST(test_clocks_sc_get_meta_requires_hello_first);
  RUN_TEST(test_clocks_sc_get_meta_returns_identity_fields);
  RUN_TEST(test_clocks_sc_get_param_list_returns_empty_list_baseline);
  RUN_TEST(test_clocks_sc_get_values_returns_empty_snapshot_baseline);
  RUN_TEST(test_clocks_sc_get_param_returns_invalid_param);
  RUN_TEST(test_clocks_sc_unknown_command_returns_sc_unknown_cmd);

  return UNITY_END();
}
