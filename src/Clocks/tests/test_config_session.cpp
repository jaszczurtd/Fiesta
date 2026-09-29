#include "../../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "../../common/tests/sc_session_test_support.h"
#include "config.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/serial/hal_serial_frame.h"
#include "unity.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void setUp(void) {
  configSessionInit();
  hal_mock_serial_reset();
  hal_mock_set_millis(0);
}

void tearDown(void) {}

void test_clocks_hello_activates_session_and_reports_module(void) {
  const char *inner = sendSerialLine("HELLO\n");
  TEST_ASSERT_NOT_NULL(inner);

  TEST_ASSERT_TRUE(configSessionActive());
  TEST_ASSERT_NOT_EQUAL(0u, configSessionId());

  TEST_ASSERT_NOT_EQUAL(NULL, strstr(inner, "OK HELLO"));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(inner, "module=" SC_MODULE_TOKEN_CLOCKS));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(inner, "proto=1"));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(inner, "fw=" FW_VERSION));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(inner, "build="));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(inner, "uid="));
}

void test_clocks_non_framed_input_is_silently_dropped(void) {
  const char *response = sendRawSerialLine("WHAT\n");
  TEST_ASSERT_FALSE(configSessionActive());
  TEST_ASSERT_EQUAL_UINT32(0u, configSessionId());
  TEST_ASSERT_EQUAL_STRING("", response);
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
  TEST_ASSERT_NOT_NULL(strstr(response, "module=" SC_MODULE_TOKEN_CLOCKS));
  TEST_ASSERT_NOT_NULL(strstr(response, "proto=1"));
  TEST_ASSERT_NOT_NULL(strstr(response, "session="));
  TEST_ASSERT_NOT_NULL(strstr(response, "fw=" FW_VERSION));
  TEST_ASSERT_NOT_NULL(strstr(response, "build="));
  TEST_ASSERT_NOT_NULL(strstr(response, "uid="));
}

void test_clocks_sc_get_param_list_returns_empty_list_baseline(void) {
  performHello();

  const char *response = sendSerialLine("SC_GET_PARAM_LIST\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK PARAM_LIST"));
  TEST_ASSERT_NOT_NULL(strstr(response, "coolant_warn_c"));
  TEST_ASSERT_NOT_NULL(strstr(response, "egt_max_c"));
}

void test_clocks_sc_get_values_returns_empty_snapshot_baseline(void) {
  performHello();

  const char *response = sendSerialLine("SC_GET_VALUES\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK PARAM_VALUES"));
  TEST_ASSERT_NOT_NULL(strstr(response, "coolant_warn_c="));
  TEST_ASSERT_NOT_NULL(strstr(response, "egt_max_c="));
}

void test_clocks_sc_get_param_known_id_returns_value_and_bounds(void) {
  performHello();

  const char *response = sendSerialLine("SC_GET_PARAM coolant_warn_c\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK PARAM"));
  TEST_ASSERT_NOT_NULL(strstr(response, "id=coolant_warn_c"));
  TEST_ASSERT_NOT_NULL(strstr(response, "value="));
  TEST_ASSERT_NOT_NULL(strstr(response, "min="));
  TEST_ASSERT_NOT_NULL(strstr(response, "max="));
  TEST_ASSERT_NOT_NULL(strstr(response, "default="));
  TEST_ASSERT_NOT_NULL(strstr(response, "group=coolant"));
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

/* Phase 8.4 - Clocks intentionally does NOT wire the SET_PARAM /
 * COMMIT_PARAMS / REVERT_PARAMS branches. All Clocks descriptors are
 * read-only, so the helper would reject every id with
 * SC_BAD_REQUEST read_only anyway; rather than add per-module code
 * for the same effect, the dispatcher's default branch returns
 * SC_UNKNOWN_CMD. This test locks that decision in. */
void test_clocks_sc_set_param_returns_sc_unknown_cmd(void) {
  performHello();

  const char *response = sendSerialLine("SC_SET_PARAM coolant_warn_c 100\n");
  TEST_ASSERT_EQUAL_STRING("SC_UNKNOWN_CMD", response);
}

void test_clocks_framed_hello_responds_with_same_seq(void) {
  char frame[80];
  buildFrame(55u, "HELLO", frame, sizeof(frame));

  const char *response = sendRawSerialLine(frame);
  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_EQUAL_STRING_LEN("$SC,55,", response, 7);
  TEST_ASSERT_NOT_NULL(strstr(response, "OK HELLO"));
  TEST_ASSERT_NOT_NULL(strstr(response, "module=" SC_MODULE_TOKEN_CLOCKS));
  TEST_ASSERT_TRUE(configSessionActive());
}

void test_clocks_framed_request_with_bad_crc_is_silently_dropped(void) {
  char frame[80];
  buildFrame(56u, "HELLO", frame, sizeof(frame));
  /* Flip the low CRC nibble. */
  char *star = strrchr(frame, '*');
  TEST_ASSERT_NOT_NULL(star);
  char *low = star + 2;
  *low = (char)((*low == '0') ? '1' : '0');

  const char *response = sendRawSerialLine(frame);
  TEST_ASSERT_EQUAL_STRING("", response);
  TEST_ASSERT_FALSE(configSessionActive());
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_clocks_hello_activates_session_and_reports_module);
  RUN_TEST(test_clocks_non_framed_input_is_silently_dropped);
  RUN_TEST(test_clocks_sc_get_meta_requires_hello_first);
  RUN_TEST(test_clocks_sc_get_meta_returns_identity_fields);
  RUN_TEST(test_clocks_sc_get_param_list_returns_empty_list_baseline);
  RUN_TEST(test_clocks_sc_get_values_returns_empty_snapshot_baseline);
  RUN_TEST(test_clocks_sc_get_param_known_id_returns_value_and_bounds);
  RUN_TEST(test_clocks_sc_get_param_returns_invalid_param);
  RUN_TEST(test_clocks_sc_unknown_command_returns_sc_unknown_cmd);
  RUN_TEST(test_clocks_sc_set_param_returns_sc_unknown_cmd);
  RUN_TEST(test_clocks_framed_hello_responds_with_same_seq);
  RUN_TEST(test_clocks_framed_request_with_bad_crc_is_silently_dropped);

  return UNITY_END();
}
