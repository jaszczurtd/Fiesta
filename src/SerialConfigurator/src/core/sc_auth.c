#include "sc_auth.h"

#include <string.h>

static const uint8_t k_sc_auth_salt[SC_AUTH_SCHEME_TAG_LEN] = {
    /* "FIESTA-SC-AUTH-v1" */
    'F','I','E','S','T','A','-','S','C','-','A','U','T','H','-','v','1'
};

const uint8_t *sc_auth_salt(void)
{
    return k_sc_auth_salt;
}

size_t sc_auth_salt_len(void)
{
    return sizeof(k_sc_auth_salt);
}

bool sc_auth_derive_device_key(
    const uint8_t *uid,
    size_t uid_len,
    uint8_t out_key[SC_AUTH_KEY_BYTES])
{
    if (uid == NULL || uid_len == 0u || out_key == NULL) {
        return false;
    }
    return sc_crypto_hmac_sha256(k_sc_auth_salt, sizeof(k_sc_auth_salt),
                                 uid, uid_len,
                                 out_key);
}

bool sc_auth_compute_response(
    const uint8_t device_key[SC_AUTH_KEY_BYTES],
    const uint8_t *challenge,
    size_t challenge_len,
    uint32_t session_id,
    uint8_t out_response[SC_AUTH_RESPONSE_BYTES])
{
    if (device_key == NULL || challenge == NULL || challenge_len == 0u ||
        out_response == NULL) {
        return false;
    }
    if (challenge_len > SC_AUTH_CHALLENGE_BYTES) {
        return false;
    }

    uint8_t message[SC_AUTH_CHALLENGE_BYTES + 4u];
    memcpy(message, challenge, challenge_len);
    /* Big-endian session id, byte-for-byte compatible with the firmware
     * helper hal_u32_to_bytes_be(). */
    message[challenge_len + 0u] = (uint8_t)(session_id >> 24);
    message[challenge_len + 1u] = (uint8_t)(session_id >> 16);
    message[challenge_len + 2u] = (uint8_t)(session_id >> 8);
    message[challenge_len + 3u] = (uint8_t)session_id;

    return sc_crypto_hmac_sha256(device_key, SC_AUTH_KEY_BYTES,
                                 message, challenge_len + 4u,
                                 out_response);
}

bool sc_auth_compute_response_hex(
    const uint8_t *uid,
    size_t uid_len,
    const uint8_t *challenge,
    size_t challenge_len,
    uint32_t session_id,
    char *out_hex,
    size_t out_hex_size)
{
    if (out_hex == NULL || out_hex_size < SC_AUTH_RESPONSE_HEX_BUF_SIZE) {
        if (out_hex != NULL && out_hex_size > 0u) {
            out_hex[0] = '\0';
        }
        return false;
    }

    uint8_t key[SC_AUTH_KEY_BYTES];
    if (!sc_auth_derive_device_key(uid, uid_len, key)) {
        out_hex[0] = '\0';
        return false;
    }

    uint8_t mac[SC_AUTH_RESPONSE_BYTES];
    if (!sc_auth_compute_response(key, challenge, challenge_len,
                                  session_id, mac)) {
        out_hex[0] = '\0';
        return false;
    }

    static const char k_hex[] = "0123456789abcdef";
    for (size_t i = 0u; i < SC_AUTH_RESPONSE_BYTES; ++i) {
        out_hex[i * 2u] = k_hex[(mac[i] >> 4) & 0x0Fu];
        out_hex[i * 2u + 1u] = k_hex[mac[i] & 0x0Fu];
    }
    out_hex[SC_AUTH_RESPONSE_BYTES * 2u] = '\0';
    return true;
}

bool sc_auth_decode_hex(const char *hex, uint8_t *out, size_t out_len)
{
    if (hex == NULL || out == NULL) {
        return false;
    }
    for (size_t i = 0u; i < out_len; ++i) {
        uint8_t v = 0u;
        for (uint8_t k = 0u; k < 2u; ++k) {
            const char c = hex[i * 2u + k];
            uint8_t nibble;
            if (c >= '0' && c <= '9') {
                nibble = (uint8_t)(c - '0');
            } else if (c >= 'A' && c <= 'F') {
                nibble = (uint8_t)(10 + (c - 'A'));
            } else if (c >= 'a' && c <= 'f') {
                nibble = (uint8_t)(10 + (c - 'a'));
            } else {
                return false;
            }
            v = (uint8_t)((v << 4) | nibble);
        }
        out[i] = v;
    }
    /* Reject if there are extra non-NUL chars after expected length. */
    if (hex[out_len * 2u] != '\0') {
        return false;
    }
    return true;
}
