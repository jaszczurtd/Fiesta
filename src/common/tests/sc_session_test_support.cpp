#include "sc_session_test_support.h"

#include "config.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/security/hal_sc_auth.h"
#include "hal/serial/hal_serial_frame.h"
#include "hal/system/hal_system.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

uint8_t refCrc8(const char *data, size_t len) {
  uint8_t crc = 0u;
  for (size_t i = 0u; i < len; ++i) {
    crc ^= (uint8_t)data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (uint8_t)((crc & 0x80u) ? ((crc << 1) ^ 0x07u) : (crc << 1));
    }
  }
  return crc;
}

static uint16_t s_test_seq = 0u;

uint16_t nextTestSeq(void) {
  s_test_seq = (uint16_t)(s_test_seq + 1u);
  if (s_test_seq == 0u) {
    s_test_seq = 1u;
  }
  return s_test_seq;
}

void buildFrame(uint16_t seq, const char *payload, char *out, size_t out_size) {
  char body[160];
  const int body_len =
      snprintf(body, sizeof(body), "SC,%u,%s", (unsigned)seq, payload);
  TEST_ASSERT_TRUE(body_len > 0 && (size_t)body_len < sizeof(body));
  const uint8_t crc = refCrc8(body, (size_t)body_len);
  const int written = snprintf(out, out_size, "$%s*%02X\n", body, crc);
  TEST_ASSERT_TRUE(written > 0 && (size_t)written < out_size);
}

const char *sendRawSerialLine(const char *line) {
  hal_mock_serial_reset();
  hal_mock_serial_inject_rx(line, -1);
  configSessionTick();
  return hal_mock_serial_last_line();
}

const char *sendTwoLinesInOnePoll(const char *first, const char *second) {
  char frame_a[160];
  char frame_b[160];
  char both[320];
  buildFrame(nextTestSeq(), first, frame_a, sizeof(frame_a));
  buildFrame(nextTestSeq(), second, frame_b, sizeof(frame_b));
  const int written = snprintf(both, sizeof(both), "%s%s", frame_a, frame_b);
  TEST_ASSERT_TRUE(written > 0 && (size_t)written < sizeof(both));
  return sendRawSerialLine(both);
}

const char *sendSerialLine(const char *inner_with_eol) {
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

  static char unwrapped[HAL_SERIAL_FRAME_PAYLOAD_MAX + 1u];
  uint16_t got_seq = 0u;
  unwrapped[0] = '\0';
  TEST_ASSERT_TRUE_MESSAGE(
      hal_serial_frame_decode(raw, &got_seq, unwrapped, sizeof(unwrapped)),
      "firmware reply is not a valid frame");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(
      seq, got_seq, "firmware reply seq does not match request seq");
  return unwrapped;
}

void performHello(void) {
  const char *response = sendSerialLine("HELLO\n");
  TEST_ASSERT_NOT_NULL(response);
  TEST_ASSERT_NOT_NULL(strstr(response, "OK HELLO"));
  TEST_ASSERT_TRUE(configSessionActive());
  TEST_ASSERT_NOT_EQUAL(0u, configSessionId());
}

/* Drive the session through HELLO + AUTH_BEGIN + AUTH_PROVE so the
 * test starts from a fully authenticated state. Uses real HMAC-SHA256
 * crypto against the deterministic mock UID baked into hal_mock. */
void performAuth(void) {
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
  TEST_ASSERT_TRUE(hal_sc_auth_compute_response(
      key, challenge, sizeof(challenge), configSessionId(), mac));

  char hex[HAL_SC_AUTH_RESPONSE_BYTES * 2u + 1u];
  static const char kHex[] = "0123456789abcdef";
  for (size_t i = 0u; i < sizeof(mac); ++i) {
    hex[i * 2u] = kHex[(mac[i] >> 4) & 0x0Fu];
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
