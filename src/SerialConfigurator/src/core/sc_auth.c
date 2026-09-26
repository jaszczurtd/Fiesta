#include "sc_auth.h"

#include <hal/core/hal_text.h>
#include <hal/security/jh_secure_random.h>

bool sc_auth_compute_response_hex(const uint8_t *uid, size_t uid_len,
                                  const uint8_t *challenge,
                                  size_t challenge_len, uint32_t session_id,
                                  char *out_hex, size_t out_hex_size) {
  if (out_hex == NULL || out_hex_size < HAL_SC_AUTH_RESPONSE_HEX_BUF_SIZE) {
    if (out_hex != NULL && out_hex_size > 0u) {
      out_hex[0] = '\0';
    }
    return false;
  }

  uint8_t key[HAL_SC_AUTH_KEY_BYTES];
  uint8_t mac[HAL_SC_AUTH_RESPONSE_BYTES];
  const bool ok = hal_sc_auth_derive_device_key(uid, uid_len, key) &&
                  hal_sc_auth_compute_response(key, challenge, challenge_len,
                                               session_id, mac);
  jh_secure_zeroize(key, sizeof(key));
  if (!ok) {
    out_hex[0] = '\0';
    return false;
  }

  static const char k_hex[] = "0123456789abcdef";
  for (size_t i = 0u; i < sizeof(mac); ++i) {
    out_hex[i * 2u] = k_hex[(mac[i] >> 4) & 0x0Fu];
    out_hex[i * 2u + 1u] = k_hex[mac[i] & 0x0Fu];
  }
  out_hex[sizeof(mac) * 2u] = '\0';
  return true;
}

bool sc_auth_decode_hex(const char *hex, uint8_t *out, size_t out_len) {
  if (hex == NULL || out == NULL) {
    return false;
  }
  for (size_t i = 0u; i < out_len; ++i) {
    const char high = hex[i * 2u];
    /* Stop at the terminator before touching the second digit. */
    if (high == '\0' || hal_text_hex_pair_to_byte_ex(high, hex[i * 2u + 1u],
                                                     &out[i]) != HAL_OK) {
      return false;
    }
  }
  return hex[out_len * 2u] == '\0';
}
