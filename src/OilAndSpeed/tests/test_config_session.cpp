#include "unity.h"
#include "config.h"
#include "../../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "hal/hal_serial_frame.h"
#include "hal/impl/.mock/hal_mock.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Independent reference CRC-8/CCITT (poly 0x07, init 0x00). Kept distinct
 * from the production helper so the wire format is locked in by the test. */
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

static const char *sendRawSerialLine(const char *line) {
  hal_mock_serial_reset();
  hal_mock_serial_inject_rx(line, -1);
  configSessionTick();
  return hal_mock_serial_last_line();
}

static uint16_t s_test_seq = 0u;
static uint16_t nextTestSeq(void) {
  s_test_seq = (uint16_t)(s_test_seq + 1u);
  if (s_test_seq == 0u) { s_test_seq = 1u; }
  return s_test_seq;
}

/* Wrap @p inner in a fresh frame, inject it, and return the unwrapped
 * inner payload of the response so existing substring assertions still
 * apply. */
static const char *sendSerialLine(const char *inner_with_eol) {
  char inner[160];
  const size_t len = strcspn(inner_with_eol, "\r\n");
  TEST_ASSERT_TRUE(len < sizeof(inner));
  memcpy(inner, inner_with_eol, len);
  inner[len] = '\0';

  const uint16_t seq = nextTestSeq();
  char frame[200];
  buildFrame(seq, inner, frame, sizeof(frame));

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
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(seq, got_seq,
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

void test_oilspeed_hello_activates_session_and_reports_module(void) {
  const char *inner = sendSerialLine("HELLO\n");
  TEST_ASSERT_NOT_NULL(inner);

  TEST_ASSERT_TRUE(configSessionActive());
  TEST_ASSERT_NOT_EQUAL(0u, configSessionId());

  TEST_ASSERT_NOT_EQUAL(NULL, strstr(inner, "OK HELLO"));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(inner, "module=" SC_MODULE_TOKEN_OIL_AND_SPEED));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(inner, "proto=1"));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(inner, "fw=" FW_VERSION));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(inner, "build="));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(inner, "uid="));
}

void test_oilspeed_non_framed_input_is_silently_dropped(void) {
  const char *response = sendRawSerialLine("WHAT\n");
  TEST_ASSERT_FALSE(configSessionActive());
  TEST_ASSERT_EQUAL_UINT32(0u, configSessionId());
  TEST_ASSERT_EQUAL_STRING("", response);
}

void test_oilspeed_config_session_init_resets_active_state(void) {
  performHello();

  TEST_ASSERT_TRUE(configSessionActive());
  TEST_ASSERT_NOT_EQUAL(0u, configSessionId());

  configSessionInit();

  TEST_ASSERT_FALSE(configSessionActive());
  TEST_ASSERT_EQUAL_UINT32(0u, configSessionId());
}

void test_oilspeed_hello_uses_current_millis_in_session_id(void) {
  hal_mock_set_millis(1234u);
  const char *response = sendSerialLine("HELLO\n");

  TEST_ASSERT_TRUE(configSessionActive());
  TEST_ASSERT_EQUAL_UINT32((((uint32_t)1u << 20) ^ 1234u), configSessionId());
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(response, "session=1049810"));
}

void test_oilspeed_handles_multiple_hello_commands_in_one_rx_buffer(void) {
  /* Stream two framed HELLOs back-to-back to verify the byte parser keeps
   * the session active across boundaries and drains them in order. */
  char frame_a[160];
  char frame_b[160];
  buildFrame(nextTestSeq(), "HELLO", frame_a, sizeof(frame_a));
  buildFrame(nextTestSeq(), "HELLO", frame_b, sizeof(frame_b));

  char both[400];
  TEST_ASSERT_TRUE(strlen(frame_a) + strlen(frame_b) < sizeof(both));
  snprintf(both, sizeof(both), "%s%s", frame_a, frame_b);

  hal_mock_serial_reset();
  hal_mock_serial_inject_rx(both, -1);
  configSessionTick();

  TEST_ASSERT_TRUE(configSessionActive());
  TEST_ASSERT_EQUAL_UINT32(((uint32_t)2u << 20), configSessionId());

  const char *raw = hal_mock_serial_last_line();
  TEST_ASSERT_NOT_NULL(raw);
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(raw, "OK HELLO"));
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(raw, "module=" SC_MODULE_TOKEN_OIL_AND_SPEED));
}

void test_oilspeed_sc_get_meta_requires_hello_first(void) {
  const char *response = sendSerialLine("SC_GET_META\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "SC_NOT_READY"));
  TEST_ASSERT_NOT_NULL(strstr(response, "HELLO_REQUIRED"));
}

void test_oilspeed_sc_get_meta_returns_identity_fields(void) {
  performHello();

  const char *response = sendSerialLine("SC_GET_META\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK META"));
  TEST_ASSERT_NOT_NULL(strstr(response, "module=" SC_MODULE_TOKEN_OIL_AND_SPEED));
  TEST_ASSERT_NOT_NULL(strstr(response, "proto=1"));
  TEST_ASSERT_NOT_NULL(strstr(response, "session="));
  TEST_ASSERT_NOT_NULL(strstr(response, "fw=" FW_VERSION));
  TEST_ASSERT_NOT_NULL(strstr(response, "build="));
  TEST_ASSERT_NOT_NULL(strstr(response, "uid="));
}

void test_oilspeed_sc_get_param_list_returns_empty_list_baseline(void) {
  performHello();

  const char *response = sendSerialLine("SC_GET_PARAM_LIST\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK PARAM_LIST"));
  TEST_ASSERT_NOT_NULL(strstr(response, "oil_pressure_read_interval_ms"));
  TEST_ASSERT_NOT_NULL(strstr(response, "thermocouple_read_interval_ms"));
}

void test_oilspeed_sc_get_values_returns_empty_snapshot_baseline(void) {
  performHello();

  const char *response = sendSerialLine("SC_GET_VALUES\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK PARAM_VALUES"));
  TEST_ASSERT_NOT_NULL(strstr(response, "oil_pressure_read_interval_ms="));
  TEST_ASSERT_NOT_NULL(strstr(response, "thermocouple_read_interval_ms="));
}

void test_oilspeed_sc_get_param_known_id_returns_value_and_bounds(void) {
  performHello();

  const char *response = sendSerialLine("SC_GET_PARAM oil_pressure_read_interval_ms\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK PARAM"));
  TEST_ASSERT_NOT_NULL(strstr(response, "id=oil_pressure_read_interval_ms"));
  TEST_ASSERT_NOT_NULL(strstr(response, "value="));
  TEST_ASSERT_NOT_NULL(strstr(response, "min="));
  TEST_ASSERT_NOT_NULL(strstr(response, "max="));
  TEST_ASSERT_NOT_NULL(strstr(response, "default="));
}

void test_oilspeed_sc_get_param_returns_invalid_param(void) {
  performHello();

  const char *response = sendSerialLine("SC_GET_PARAM nominal_rpm\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "SC_INVALID_PARAM_ID"));
  TEST_ASSERT_NOT_NULL(strstr(response, "id=nominal_rpm"));
}

void test_oilspeed_sc_unknown_command_returns_sc_unknown_cmd(void) {
  performHello();

  const char *response = sendSerialLine("SC_DO_SOMETHING\n");
  TEST_ASSERT_EQUAL_STRING("SC_UNKNOWN_CMD", response);
}

/* Phase 8.4 — OilAndSpeed intentionally does NOT wire the SET_PARAM /
 * COMMIT_PARAMS / REVERT_PARAMS branches. All descriptors here are
 * read-only, so the helper would reject every id with
 * SC_BAD_REQUEST read_only anyway; the dispatcher's default branch
 * returns SC_UNKNOWN_CMD instead, mirroring Clocks. This test locks
 * that decision in. */
void test_oilspeed_sc_set_param_returns_sc_unknown_cmd(void) {
  performHello();

  const char *response =
      sendSerialLine("SC_SET_PARAM oil_pressure_read_interval_ms 1500\n");
  TEST_ASSERT_EQUAL_STRING("SC_UNKNOWN_CMD", response);
}

void test_oilspeed_framed_hello_responds_with_same_seq(void) {
  char frame[80];
  buildFrame(77u, "HELLO", frame, sizeof(frame));

  const char *response = sendRawSerialLine(frame);
  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_EQUAL_STRING_LEN("$SC,77,", response, 7);
  TEST_ASSERT_NOT_NULL(strstr(response, "OK HELLO"));
  TEST_ASSERT_NOT_NULL(strstr(response, "module=" SC_MODULE_TOKEN_OIL_AND_SPEED));
  TEST_ASSERT_TRUE(configSessionActive());
}

void test_oilspeed_framed_request_with_bad_crc_is_silently_dropped(void) {
  char frame[80];
  buildFrame(78u, "HELLO", frame, sizeof(frame));
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

  RUN_TEST(test_oilspeed_hello_activates_session_and_reports_module);
  RUN_TEST(test_oilspeed_non_framed_input_is_silently_dropped);
  RUN_TEST(test_oilspeed_config_session_init_resets_active_state);
  RUN_TEST(test_oilspeed_hello_uses_current_millis_in_session_id);
  RUN_TEST(test_oilspeed_handles_multiple_hello_commands_in_one_rx_buffer);
  RUN_TEST(test_oilspeed_sc_get_meta_requires_hello_first);
  RUN_TEST(test_oilspeed_sc_get_meta_returns_identity_fields);
  RUN_TEST(test_oilspeed_sc_get_param_list_returns_empty_list_baseline);
  RUN_TEST(test_oilspeed_sc_get_values_returns_empty_snapshot_baseline);
  RUN_TEST(test_oilspeed_sc_get_param_known_id_returns_value_and_bounds);
  RUN_TEST(test_oilspeed_sc_get_param_returns_invalid_param);
  RUN_TEST(test_oilspeed_sc_unknown_command_returns_sc_unknown_cmd);
  RUN_TEST(test_oilspeed_sc_set_param_returns_sc_unknown_cmd);
  RUN_TEST(test_oilspeed_framed_hello_responds_with_same_seq);
  RUN_TEST(test_oilspeed_framed_request_with_bad_crc_is_silently_dropped);

  return UNITY_END();
}
