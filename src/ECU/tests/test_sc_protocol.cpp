#include "unity.h"

#include "config.h"
#include "hal/hal_serial_frame.h"
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
    TEST_ASSERT_NOT_NULL(strstr(response, "module=ECU"));

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
    TEST_ASSERT_NOT_NULL(strstr(meta_response, "module=ECU"));
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
    RUN_TEST(test_non_framed_lines_are_silently_discarded);
    RUN_TEST(test_framed_hello_responds_with_same_seq_and_valid_crc);
    RUN_TEST(test_framed_request_with_bad_crc_is_silently_dropped);
    RUN_TEST(test_framed_command_after_framed_hello_preserves_seq);

    return UNITY_END();
}
