#include "unity.h"

#include "config.h"
#include "dtcManager.h"
#include "testable/ecuParams_testable.h"
#include "../../common/scDefinitions/sc_fiesta_module_tokens.h"
#include "hal/hal_eeprom.h"
#include "hal/hal_kv.h"
#include "hal/hal_sc_auth.h"
#include "hal/hal_serial_frame.h"
#include "hal/hal_system.h"
#include "hal/impl/.mock/hal_mock.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" const char *test_stubs_last_forwarded_serial_line(void);
extern "C" unsigned test_stubs_forwarded_serial_count(void);
extern "C" void test_stubs_reset_forwarded_serial(void);

/* Independent reference CRC-8/CCITT (poly 0x07, init 0x00) used both by
 * the wire-format tests and by the auto-framing helper. Kept separate from
 * the production helper so the wire format is locked in by the test. */
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

/* Inject a fully-formed line as-is and return whatever firmware emitted. */
static const char *sendRawSerialLine(const char *line) {
    hal_mock_serial_reset();
    hal_mock_serial_inject_rx(line, -1);
    configSessionTick();
    return hal_mock_serial_last_line();
}

/* Wrap @p inner in a fresh frame, inject it, and return the unwrapped
 * inner payload of the response. The session-level seq is monotonic across
 * the test process so that sequential calls exercise the correlation logic. */
static uint16_t s_test_seq = 0u;
static const char *sendSerialLine(const char *inner_with_eol) {
    /* Strip trailing CR/LF so we can re-add it after framing. */
    char inner[160];
    const size_t len = strcspn(inner_with_eol, "\r\n");
    TEST_ASSERT_TRUE(len < sizeof(inner));
    memcpy(inner, inner_with_eol, len);
    inner[len] = '\0';

    s_test_seq = (uint16_t)(s_test_seq + 1u);
    if (s_test_seq == 0u) {
        s_test_seq = 1u;
    }

    char frame[200];
    buildFrame(s_test_seq, inner, frame, sizeof(frame));

    const char *raw = sendRawSerialLine(frame);
    if ((raw == NULL) || (raw[0] == '\0')) {
        return raw;
    }

    /* Decode wire-frame back to inner payload so the existing assertions
     * (which check substrings like "SC_OK META") keep working unchanged. */
    static char unwrapped[200];
    uint16_t got_seq = 0u;
    unwrapped[0] = '\0';
    const bool ok = hal_serial_frame_decode(raw, &got_seq, unwrapped, sizeof(unwrapped));
    TEST_ASSERT_TRUE_MESSAGE(ok, "firmware reply is not a valid frame");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(s_test_seq, got_seq,
                                     "firmware reply seq does not match request seq");
    return unwrapped;
}

static void performHello(void) {
    const char *response = sendSerialLine("HELLO\n");
    TEST_ASSERT_NOT_NULL(response);
    TEST_ASSERT_NOT_NULL(strstr(response, "OK HELLO"));
    TEST_ASSERT_TRUE(configSessionActive());
    TEST_ASSERT_NOT_EQUAL(0u, configSessionId());
}

/* Drive the session through HELLO + AUTH_BEGIN + AUTH_PROVE so the
 * test starts from a fully authenticated state. Uses real HMAC-SHA256
 * crypto against the deterministic mock UID baked into hal_mock. */
static void performAuth(void) {
    performHello();

    const char *challengeReply = sendSerialLine("SC_AUTH_BEGIN\n");
    TEST_ASSERT_NOT_NULL(challengeReply);
    const char *hexStart = strstr(challengeReply, "AUTH_CHALLENGE ");
    TEST_ASSERT_NOT_NULL_MESSAGE(hexStart, "expected AUTH_CHALLENGE in reply");
    hexStart += strlen("AUTH_CHALLENGE ");

    uint8_t challenge[HAL_SC_AUTH_CHALLENGE_BYTES];
    for (size_t i = 0u; i < HAL_SC_AUTH_CHALLENGE_BYTES; ++i) {
        unsigned byte = 0u;
        TEST_ASSERT_EQUAL_INT(1, sscanf(hexStart + i * 2u, "%2x", &byte));
        challenge[i] = (uint8_t)byte;
    }

    uint8_t uid[HAL_DEVICE_UID_BYTES];
    hal_get_device_uid(uid);
    uint8_t key[HAL_SC_AUTH_KEY_BYTES];
    TEST_ASSERT_TRUE(hal_sc_auth_derive_device_key(uid, sizeof(uid), key));

    uint8_t mac[HAL_SC_AUTH_RESPONSE_BYTES];
    TEST_ASSERT_TRUE(hal_sc_auth_compute_response(key, challenge,
                                                  sizeof(challenge),
                                                  configSessionId(), mac));

    char hex[HAL_SC_AUTH_RESPONSE_BYTES * 2u + 1u];
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0u; i < sizeof(mac); ++i) {
        hex[i * 2u]      = kHex[(mac[i] >> 4) & 0x0Fu];
        hex[i * 2u + 1u] = kHex[mac[i] & 0x0Fu];
    }
    hex[sizeof(mac) * 2u] = '\0';

    char proveLine[256];
    snprintf(proveLine, sizeof(proveLine), "SC_AUTH_PROVE %s\n", hex);
    const char *proveReply = sendSerialLine(proveLine);
    TEST_ASSERT_NOT_NULL(proveReply);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(proveReply, "AUTH_OK"),
                                 "expected SC_OK AUTH_OK after PROVE");
}

void setUp(void) {
    hal_mock_set_millis(0);
    /* Wipe any blob that previous tests in this program may have
     * persisted. ecuParamsInit then loads pristine defaults. */
    (void)hal_kv_delete(ecuParamsBlobKeyForTest());
    ecuParamsResetRuntimeStateForTest();
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
    TEST_ASSERT_NOT_NULL(strstr(response, "module=" SC_MODULE_TOKEN_ECU));
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
    /* Phase 8.6: descriptor group surfaces in the reply so the host
     * GUI can section the Values tab. */
    TEST_ASSERT_NOT_NULL(strstr(response, "group=idle"));
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

void test_non_framed_lines_are_silently_discarded(void) {
    /* Plain-text input has no place in the framed-only protocol. The HAL
     * session must silently drop it without invoking the unknown-line
     * handler or producing any reply. */
    const char *response = sendRawSerialLine("WHAT\n");
    TEST_ASSERT_EQUAL_STRING("", response);
    TEST_ASSERT_EQUAL_UINT(0u, test_stubs_forwarded_serial_count());
    TEST_ASSERT_FALSE(configSessionActive());

    /* A line that contains the frame sentinel as a substring (e.g. inside
     * a debug log) must also be ignored - only lines that *start* with
     * `$SC,` are accepted. */
    const char *response2 = sendRawSerialLine("[INFO] saw $SC,1,HELLO*00\n");
    TEST_ASSERT_EQUAL_STRING("", response2);
    TEST_ASSERT_FALSE(configSessionActive());
}

/* ------------------------------------------------------------------ */
/* Wire-framing tests: $SC,<seq>,<payload>*<crc8>\n with CRC-8/CCITT. */
/* ------------------------------------------------------------------ */

void test_framed_hello_responds_with_same_seq_and_valid_crc(void) {
    char frame[160];
    buildFrame(7u, "HELLO", frame, sizeof(frame));

    const char *response = sendRawSerialLine(frame);
    TEST_ASSERT_NOT_NULL(response);
    TEST_ASSERT_EQUAL_STRING_LEN("$SC,7,", response, 6);
    TEST_ASSERT_NOT_NULL(strstr(response, "OK HELLO"));
    TEST_ASSERT_NOT_NULL(strstr(response, "module=" SC_MODULE_TOKEN_ECU));

    /* Validate CRC of the response over the bytes between '$' and '*'. */
    const char *star = strrchr(response, '*');
    TEST_ASSERT_NOT_NULL(star);
    const size_t body_len = (size_t)(star - (response + 1));
    const uint8_t expected = refCrc8(response + 1, body_len);
    unsigned actual = 0u;
    TEST_ASSERT_EQUAL_INT(1, sscanf(star + 1, "%2x", &actual));
    TEST_ASSERT_EQUAL_UINT(expected, (uint8_t)actual);
}

void test_framed_request_with_bad_crc_is_silently_dropped(void) {
    char frame[160];
    buildFrame(11u, "HELLO", frame, sizeof(frame));
    /* Corrupt the CRC by flipping the low nibble. */
    char *star = strrchr(frame, '*');
    TEST_ASSERT_NOT_NULL(star);
    char *low = star + 2;
    *low = (char)((*low == '0') ? '1' : '0');

    const char *response = sendRawSerialLine(frame);
    TEST_ASSERT_EQUAL_STRING("", response);
    TEST_ASSERT_EQUAL_UINT(0u, test_stubs_forwarded_serial_count());
    TEST_ASSERT_FALSE(configSessionActive());
}

void test_framed_command_after_framed_hello_preserves_seq(void) {
    char hello_frame[160];
    buildFrame(100u, "HELLO", hello_frame, sizeof(hello_frame));
    const char *hello_response = sendRawSerialLine(hello_frame);
    TEST_ASSERT_NOT_NULL(hello_response);
    TEST_ASSERT_EQUAL_STRING_LEN("$SC,100,", hello_response, 8);

    char meta_frame[160];
    buildFrame(101u, "SC_GET_META", meta_frame, sizeof(meta_frame));
    const char *meta_response = sendRawSerialLine(meta_frame);
    TEST_ASSERT_NOT_NULL(meta_response);
    TEST_ASSERT_EQUAL_STRING_LEN("$SC,101,", meta_response, 8);
    TEST_ASSERT_NOT_NULL(strstr(meta_response, "SC_OK META"));
    TEST_ASSERT_NOT_NULL(strstr(meta_response, "module=" SC_MODULE_TOKEN_ECU));
}

/* ------------------------------------------------------------------ */
/* Phase 8.3: SET_PARAM / COMMIT_PARAMS / REVERT_PARAMS over the wire. */
/* ------------------------------------------------------------------ */

void test_sc_set_param_rejects_without_auth(void) {
    performHello();
    /* No AUTH_PROVE. */
    const char *response = sendSerialLine("SC_SET_PARAM fan_coolant_start_c 110\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_NOT_AUTHORIZED"));
    /* Active untouched. */
    TEST_ASSERT_EQUAL_INT16(TEMP_FAN_START, ecuParamsFanCoolantStart());
}

void test_sc_commit_params_rejects_without_auth(void) {
    performHello();
    const char *response = sendSerialLine("SC_COMMIT_PARAMS\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_NOT_AUTHORIZED"));
}

void test_sc_revert_params_rejects_without_auth(void) {
    performHello();
    const char *response = sendSerialLine("SC_REVERT_PARAMS\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_NOT_AUTHORIZED"));
}

void test_sc_set_param_writes_only_staging_after_auth(void) {
    performAuth();

    const char *response = sendSerialLine("SC_SET_PARAM fan_coolant_start_c 110\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_OK PARAM_SET"));
    TEST_ASSERT_NOT_NULL(strstr(response, "id=fan_coolant_start_c"));
    TEST_ASSERT_NOT_NULL(strstr(response, "staged=110"));
    /* Active still at default until COMMIT. */
    char activeFragment[32];
    snprintf(activeFragment, sizeof(activeFragment), "active=%d", TEMP_FAN_START);
    TEST_ASSERT_NOT_NULL(strstr(response, activeFragment));
    TEST_ASSERT_EQUAL_INT16(TEMP_FAN_START, ecuParamsFanCoolantStart());
}

void test_sc_set_param_rejects_out_of_range(void) {
    performAuth();

    const char *response = sendSerialLine("SC_SET_PARAM fan_coolant_start_c 200\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_BAD_REQUEST"));
    TEST_ASSERT_NOT_NULL(strstr(response, "out_of_range"));
    TEST_ASSERT_NOT_NULL(strstr(response, "id=fan_coolant_start_c"));
    TEST_ASSERT_NOT_NULL(strstr(response, "min=70"));
    TEST_ASSERT_NOT_NULL(strstr(response, "max=130"));
    TEST_ASSERT_EQUAL_INT16(TEMP_FAN_START, ecuParamsFanCoolantStart());
}

void test_sc_set_param_rejects_unknown_id(void) {
    performAuth();

    const char *response = sendSerialLine("SC_SET_PARAM nope 0\n");
    TEST_ASSERT_NOT_NULL(strstr(response, "SC_INVALID_PARAM_ID"));
    TEST_ASSERT_NOT_NULL(strstr(response, "id=nope"));
}

void test_sc_commit_persists_blob_and_updates_active(void) {
    performAuth();

    /* Step the staged value away from the default. */
    (void)sendSerialLine("SC_SET_PARAM fan_coolant_start_c 109\n");

    const char *commitReply = sendSerialLine("SC_COMMIT_PARAMS\n");
    TEST_ASSERT_NOT_NULL(strstr(commitReply, "SC_OK PARAMS_COMMITTED"));
    TEST_ASSERT_NOT_NULL(strstr(commitReply, "count=6"));
    /* Active mirror reflects the new value immediately. */
    TEST_ASSERT_EQUAL_INT16(109, ecuParamsFanCoolantStart());

    /* Reset runtime state and reload from KV: the persisted blob must
     * round-trip the new value. */
    ecuParamsResetRuntimeStateForTest();
    ecuParamsInit();
    TEST_ASSERT_EQUAL_INT16(109, ecuParamsFanCoolantStart());
}

void test_sc_commit_fails_on_hysteresis_rule(void) {
    performAuth();

    /* fan_coolant_stop_c >= fan_coolant_start_c violates hysteresis. */
    (void)sendSerialLine("SC_SET_PARAM fan_coolant_stop_c 110\n");

    const char *commitReply = sendSerialLine("SC_COMMIT_PARAMS\n");
    TEST_ASSERT_NOT_NULL(strstr(commitReply, "SC_COMMIT_FAILED"));
    TEST_ASSERT_NOT_NULL(strstr(commitReply, "reason=fan_coolant_hysteresis"));
    /* Active and persisted state untouched. */
    TEST_ASSERT_EQUAL_INT16(TEMP_FAN_STOP, ecuParamsFanCoolantStop());
    TEST_ASSERT_EQUAL_INT16(TEMP_FAN_START, ecuParamsFanCoolantStart());
}

void test_sc_revert_restores_staging_from_active(void) {
    performAuth();

    /* Mutate staging away from active, REVERT, then COMMIT — the blob
     * must reflect the original defaults rather than the staged 110. */
    (void)sendSerialLine("SC_SET_PARAM fan_coolant_start_c 110\n");
    const char *revertReply = sendSerialLine("SC_REVERT_PARAMS\n");
    TEST_ASSERT_NOT_NULL(strstr(revertReply, "SC_OK PARAMS_REVERTED"));

    const char *commitReply = sendSerialLine("SC_COMMIT_PARAMS\n");
    TEST_ASSERT_NOT_NULL(strstr(commitReply, "SC_OK PARAMS_COMMITTED"));

    ecuParamsResetRuntimeStateForTest();
    ecuParamsInit();
    /* If revert had failed, the blob would carry 110. */
    TEST_ASSERT_EQUAL_INT16(TEMP_FAN_START, ecuParamsFanCoolantStart());
}

void test_sc_set_param_rejects_malformed_payload(void) {
    performAuth();

    /* Missing value. */
    const char *missingValue = sendSerialLine("SC_SET_PARAM fan_coolant_start_c\n");
    TEST_ASSERT_NOT_NULL(strstr(missingValue, "SC_BAD_REQUEST"));

    /* Non-numeric value. */
    const char *badValue = sendSerialLine("SC_SET_PARAM fan_coolant_start_c xyz\n");
    TEST_ASSERT_NOT_NULL(strstr(badValue, "SC_BAD_REQUEST"));
    TEST_ASSERT_NOT_NULL(strstr(badValue, "value_not_int16"));

    /* Out of int16 domain. */
    const char *outOfDomain = sendSerialLine("SC_SET_PARAM fan_coolant_start_c 70000\n");
    TEST_ASSERT_NOT_NULL(strstr(outOfDomain, "SC_BAD_REQUEST"));
    TEST_ASSERT_NOT_NULL(strstr(outOfDomain, "value_not_int16"));
}

int main(void) {
    /* Phase 8.3 brought a production caller of ecuParamsPersist (the
     * SC_COMMIT_PARAMS branch), so KV-backed persistence has to work
     * during these tests. Older tests in this suite never wrote a
     * blob, so the eeprom + kv init calls were unnecessary then.
     * dtcManagerInit additionally invokes hal_kv_init under the hood,
     * which is why test_ecu_params.cpp (the only other suite that
     * exercises persistence) takes the same shape. */
    hal_mock_eeprom_reset();
    hal_eeprom_init(HAL_EEPROM_RP2040, ECU_EEPROM_SIZE_BYTES, 0);
    dtcManagerInit();

    UNITY_BEGIN();

    RUN_TEST(test_sc_get_meta_requires_hello_first);
    RUN_TEST(test_sc_get_meta_returns_sc_ok_with_identity_fields);
    RUN_TEST(test_sc_get_param_list_returns_all_known_ids);
    RUN_TEST(test_sc_get_param_returns_value_and_bounds);
    RUN_TEST(test_sc_get_param_rejects_unknown_parameter);
    RUN_TEST(test_sc_get_param_requires_argument);
    RUN_TEST(test_sc_unknown_command_returns_sc_unknown_cmd);
    RUN_TEST(test_sc_get_values_returns_snapshot);
    RUN_TEST(test_non_framed_lines_are_silently_discarded);
    RUN_TEST(test_framed_hello_responds_with_same_seq_and_valid_crc);
    RUN_TEST(test_framed_request_with_bad_crc_is_silently_dropped);
    RUN_TEST(test_framed_command_after_framed_hello_preserves_seq);

    /* Phase 8.3 — auth-gated SET / COMMIT / REVERT. */
    RUN_TEST(test_sc_set_param_rejects_without_auth);
    RUN_TEST(test_sc_commit_params_rejects_without_auth);
    RUN_TEST(test_sc_revert_params_rejects_without_auth);
    RUN_TEST(test_sc_set_param_writes_only_staging_after_auth);
    RUN_TEST(test_sc_set_param_rejects_out_of_range);
    RUN_TEST(test_sc_set_param_rejects_unknown_id);
    RUN_TEST(test_sc_commit_persists_blob_and_updates_active);
    RUN_TEST(test_sc_commit_fails_on_hysteresis_rule);
    RUN_TEST(test_sc_revert_restores_staging_from_active);
    RUN_TEST(test_sc_set_param_rejects_malformed_payload);

    return UNITY_END();
}
