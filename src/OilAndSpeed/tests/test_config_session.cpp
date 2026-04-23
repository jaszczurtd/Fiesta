#include "unity.h"
#include "config.h"
#include "hal/impl/.mock/hal_mock.h"
#include <string.h>

void setUp(void) {
  configSessionInit();
  hal_mock_serial_reset();
  hal_mock_set_millis(0);
}

void tearDown(void) {}

void test_oilspeed_hello_activates_session_and_reports_module(void) {
  hal_mock_serial_inject_rx("HELLO\n", -1);
  configSessionTick();

  TEST_ASSERT_TRUE(configSessionActive());
  TEST_ASSERT_NOT_EQUAL(0u, configSessionId());

  const char *line = hal_mock_serial_last_line();
  TEST_ASSERT_NOT_NULL(line);
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "OK HELLO"));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "module=" MODULE_NAME));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(line, "proto=1"));
}

void test_oilspeed_unknown_command_returns_error(void) {
  hal_mock_serial_inject_rx("WHAT\n", -1);
  configSessionTick();

  TEST_ASSERT_FALSE(configSessionActive());
  TEST_ASSERT_EQUAL_UINT32(0u, configSessionId());
  TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN", hal_mock_serial_last_line());
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_oilspeed_hello_activates_session_and_reports_module);
  RUN_TEST(test_oilspeed_unknown_command_returns_error);

  return UNITY_END();
}
