#include "unity.h"
#include "config.h"
#include "../../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "hal/hal_serial_frame.h"
#include "hal/impl/.mock/hal_mock.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Independent reference CRC-8/CCITT - same poly/init as the production
 * helper but kept distinct so the wire format is locked in by the test. */
static uint8_t refCrc8(const char *data, size_t len) {
  uint8_t crc = 0u;
  for (size_t i = 0u; i < len; ++i) {
    crc ^= (uint8_t)data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (uint8_t)((crc & 0x80u) ? ((crc << 1) ^ 0x07u) : (crc << 1));
    }
  }
  return crc;
}

static void buildFrame(uint16_t seq, const char *payload, char *out, size_t out_size) {
  char body[160];
  const int body_len = snprintf(body, sizeof(body), "SC,%u,%s", (unsigned)seq, payload);
  TEST_ASSERT_TRUE(body_len > 0 && (size_t)body_len < sizeof(body));
  const uint8_t crc = refCrc8(body, (size_t)body_len);
  const int written = snprintf(out, out_size, "$%s*%02X\n", body, crc);
  TEST_ASSERT_TRUE(written > 0 && (size_t)written < out_size);
}

/* Inject a fully-formed wire frame (or any other line) and return the raw
 * firmware reply. */
static const char *sendRawSerialLine(const char *line) {
  hal_mock_serial_reset();
  hal_mock_serial_inject_rx(line, -1);
  configSessionTick();
  return hal_mock_serial_last_line();
}

/* Wrap @p inner in a fresh frame, inject it, and return the unwrapped
 * inner payload of the response so existing substring assertions still
 * apply. The seq is monotonic across the test process to exercise the
 * request/response correlation. */
static uint16_t s_test_seq = 0u;
static const char *sendSerialLine(const char *inner_with_eol) {
  char inner[160];
  const size_t len = strcspn(inner_with_eol, "\r\n");
  TEST_ASSERT_TRUE(len < sizeof(inner));
  memcpy(inner, inner_with_eol, len);
  inner[len] = '\0';

  s_test_seq = (uint16_t)(s_test_seq + 1u);
  if (s_test_seq == 0u) { s_test_seq = 1u; }

  char frame[200];
  buildFrame(s_test_seq, inner, frame, sizeof(frame));

  const char *raw = sendRawSerialLine(frame);
  if ((raw == NULL) || (raw[0] == '\0')) {
    return raw;
  }

  static char unwrapped[200];
  uint16_t got_seq = 0u;
  unwrapped[0] = '\0';
  TEST_ASSERT_TRUE_MESSAGE(
      hal_serial_frame_decode(raw, &got_seq, unwrapped, sizeof(unwrapped)),
      "firmware reply is not a valid frame");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(s_test_seq, got_seq,
      "firmware reply seq does not match request seq");
  return unwrapped;
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

/* Phase 8.4 — Clocks intentionally does NOT wire the SET_PARAM /
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
