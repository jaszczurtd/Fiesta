#include "../../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "../../common/tests/sc_session_test_support.h"
#include "can.h"
#include "clocksState.h"
#include "config.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/serial/hal_serial_frame.h"
#include "unity.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* CAN publication is real; drawing and backlight are unrelated to SC. */
void updateCluster(void) {}
int getLcdBrightness(void) { return 0; }

void setUp(void) {
  configSessionInit();
  hal_mock_serial_reset();
  hal_mock_set_millis(0);
  clocksStateInit();
  const clocks_local_snapshot_t empty = {};
  clocksStatePublishLocal(&empty);
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

void test_clocks_config_session_init_resets_active_state(void) {
  performHello();

  TEST_ASSERT_TRUE(configSessionActive());
  TEST_ASSERT_NOT_EQUAL(0u, configSessionId());

  configSessionInit();

  TEST_ASSERT_FALSE(configSessionActive());
  TEST_ASSERT_EQUAL_UINT32(0u, configSessionId());
}

void test_clocks_hello_uses_current_millis_in_session_id(void) {
  hal_mock_set_millis(1234u);
  const char *response = sendSerialLine("HELLO\n");

  TEST_ASSERT_TRUE(configSessionActive());
  TEST_ASSERT_EQUAL_UINT32((((uint32_t)1u << 20) ^ 1234u), configSessionId());
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(response, "session=1049810"));
}

void test_clocks_handles_multiple_hello_commands_in_one_rx_buffer(void) {
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
  TEST_ASSERT_NOT_EQUAL(NULL, strstr(raw, "module=" SC_MODULE_TOKEN_CLOCKS));
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
  // The whole catalog fits one reply, without the truncation mark.
  TEST_ASSERT_NULL(strstr(response, "*"));
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

void test_clocks_gps_is_available_as_a_command_without_receiver(void) {
  performHello();
  const char *response = sendSerialLine("SC_GET_GPS\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "GPS"));
  TEST_ASSERT_NOT_NULL(strstr(response, "available=0"));
  TEST_ASSERT_NOT_NULL(strstr(response, "lat_e6=0"));
  TEST_ASSERT_NOT_NULL(strstr(response, "epoch=0"));
}

void test_clocks_gps_reports_precise_position_and_utc_then_expires(void) {
  clocks_local_snapshot_t local = {};
  local.gps.available = local.gps.utcValid = true;
  local.gps.latitude = 52.2296756;
  local.gps.longitude = -123.1234566;
  local.gps.speedKmh = 15.06;
  local.gps.epochUtc = 1793018096u;
  clocksStatePublishLocal(&local);
  performHello();
  const char *response = sendSerialLine("SC_GET_GPS\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "available=1"));
  TEST_ASSERT_NOT_NULL(strstr(response, "lat_e6=52229676"));
  TEST_ASSERT_NOT_NULL(strstr(response, "lon_e6=-123123457"));
  TEST_ASSERT_NOT_NULL(strstr(response, "epoch=1793018096"));
  hal_mock_set_millis(GPS_UTC_MAX_AGE_MS + 1u);
  response = sendSerialLine("SC_GET_GPS\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "available=1"));
  TEST_ASSERT_NOT_NULL(strstr(response, "epoch=0"));
  hal_mock_set_millis(GPS_MAX_AGE_MS + 1u);
  response = sendSerialLine("SC_GET_GPS\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "available=0"));
  TEST_ASSERT_NOT_NULL(strstr(response, "lat_e6=0"));
}

/* Parse complete scalar tokens: valid=1 must never match valid=13. */
static int scalar(const char *reply, const char *id) {
  char key[80];
  snprintf(key, sizeof(key), " %s=", id);
  const char *at = strstr(reply, key);
  TEST_ASSERT_NOT_NULL(at);
  int value = 0;
  TEST_ASSERT_EQUAL_INT(1, sscanf(at + strlen(key), "%d", &value));
  return value;
}

void test_abs_validity_crosses_the_core_snapshot_and_sc_reply(void) {
  canSetValue(F_ABS_CAR_SPEED, 42.0f);
  canSetAbsReady(false);
  canCheckConnection();
  clocksStateRefreshView();
  performHello();
  const char *response = sendSerialLine("SC_GET_VALUES\n");
  TEST_ASSERT_EQUAL_INT(0, scalar(response, "valid"));
  TEST_ASSERT_EQUAL_INT(0, scalar(response, "abs_kmh"));
  canSetAbsReady(true);
  /* The display view must not change until core 1 refreshes its snapshot. */
  canCheckConnection();
  TEST_ASSERT_FALSE(isAbsSpeedValid());
  clocksStateRefreshView();
  response = sendSerialLine("SC_GET_VALUES\n");
  TEST_ASSERT_EQUAL_INT(1, scalar(response, "valid"));
  TEST_ASSERT_EQUAL_INT(42, scalar(response, "abs_kmh"));
  canSetValue(F_ABS_CAR_SPEED, 0.0f);
  canCheckConnection();
  clocksStateRefreshView();
  response = sendSerialLine("SC_GET_VALUES\n");
  TEST_ASSERT_EQUAL_INT(1, scalar(response, "valid"));
  TEST_ASSERT_EQUAL_INT(0, scalar(response, "abs_kmh"));
  canSetAbsReady(false);
  canCheckConnection();
  clocksStateRefreshView();
  response = sendSerialLine("SC_GET_VALUES\n");
  TEST_ASSERT_EQUAL_INT(0, scalar(response, "valid"));
}

void test_live_values_refresh_and_expire_without_the_ecu(void) {
  clocks_ecu_snapshot_t ecu = {};
  ecu.absReady = true;
  ecu.values[F_ABS_CAR_SPEED] = 42.0f;
  clocksStatePublishEcu(&ecu);
  clocksStateRefreshView();
  clocks_local_snapshot_t local = {};
  local.oilBar = 1.25f;
  local.oilValid = true;
  local.egtC[EGT_PRE_DPF] = 805.0f;
  local.egtValid[EGT_PRE_DPF] = true;
  local.egtC[EGT_IN_DPF] = 601.0f;
  local.egtValid[EGT_IN_DPF] = true;
  clocksStatePublishLocal(&local);
  performHello();
  const char *response = sendSerialLine("SC_GET_VALUES\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "abs_kmh=42"));
  TEST_ASSERT_EQUAL_INT(15, scalar(response, "valid"));
  TEST_ASSERT_NOT_NULL(strstr(response, "oil_cbar=125"));

  TEST_ASSERT_NOT_NULL(strstr(response, "egt0_c=805"));
  TEST_ASSERT_NOT_NULL(strstr(response, "egt1_c=601"));
  TEST_ASSERT_NULL(strstr(response, "*=*"));
  ecu.values[F_ABS_CAR_SPEED] = 32767.0f;
  clocksStatePublishEcu(&ecu);
  clocksStateRefreshView();
  local.oilBar = 327.66f;
  local.egtC[EGT_PRE_DPF] = -32768.0f;
  local.egtC[EGT_IN_DPF] = 32767.0f;
  clocksStatePublishLocal(&local);
  response = sendSerialLine("SC_GET_VALUES\n");
  TEST_ASSERT_EQUAL_INT(15, scalar(response, "valid"));
  TEST_ASSERT_NULL(strstr(response, "*=*"));
  TEST_ASSERT_TRUE(strlen(response) <= HAL_SERIAL_FRAME_PAYLOAD_MAX);
  local.oilBar = 2.50f;
  clocksStatePublishLocal(&local);
  response = sendSerialLine("SC_GET_VALUES\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "oil_cbar=250"));
  hal_mock_set_millis(THERMOCOUPLE_MAX_AGE_MS + OIL_PRESSURE_MAX_AGE_MS);
  response = sendSerialLine("SC_GET_VALUES\n");
  TEST_ASSERT_NOT_NULL(strstr(response, "oil_cbar=0"));
  TEST_ASSERT_EQUAL_INT(1, scalar(response, "valid"));
  TEST_ASSERT_EQUAL_INT(0, scalar(response, "egt0_c"));
  TEST_ASSERT_EQUAL_INT(0, scalar(response, "egt1_c"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_clocks_config_session_init_resets_active_state);
  RUN_TEST(test_clocks_hello_uses_current_millis_in_session_id);
  RUN_TEST(test_clocks_handles_multiple_hello_commands_in_one_rx_buffer);
  RUN_TEST(test_live_values_refresh_and_expire_without_the_ecu);
  RUN_TEST(test_abs_validity_crosses_the_core_snapshot_and_sc_reply);

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
  RUN_TEST(test_clocks_gps_is_available_as_a_command_without_receiver);
  RUN_TEST(test_clocks_gps_reports_precise_position_and_utc_then_expires);

  return UNITY_END();
}
