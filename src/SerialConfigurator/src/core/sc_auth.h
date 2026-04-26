#ifndef SC_AUTH_H
#define SC_AUTH_H

/*
 * Host-side mirror of `libraries/JaszczurHAL/src/hal/hal_sc_auth.h`.
 *
 * The firmware derives a per-device key as
 *   K_device = HMAC-SHA256(key=salt, message=uid_bytes)
 * and expects the host to reply to its challenge with
 *   response = HMAC-SHA256(key=K_device,
 *                          message=challenge || session_id_be32)
 *
 * This header carries a byte-for-byte identical salt definition and the
 * matching helpers, so the host can reproduce the same MAC. The salt is a
 * project-wide public constant; secrecy of the auth scheme rests on
 * HMAC-SHA256 + the per-device UID, not on the salt.
 *
 * Both files MUST stay in sync. If the salt or any constant changes here,
 * update the firmware mirror in the same change set.
 */

#include "sc_crypto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Authentication scheme identifier embedded in the salt. */
#define SC_AUTH_SCHEME_TAG "FIESTA-SC-AUTH-v1"

/** @brief Length of @ref SC_AUTH_SCHEME_TAG (excluding NUL). */
#define SC_AUTH_SCHEME_TAG_LEN 17u

/** @brief Per-device authentication key length (HMAC-SHA256 output). */
#define SC_AUTH_KEY_BYTES SC_CRYPTO_SHA256_DIGEST_BYTES

/** @brief Challenge nonce length in bytes (128 bits). */
#define SC_AUTH_CHALLENGE_BYTES 16u

/** @brief Buffer size for hex-encoded challenge (32 hex + NUL). */
#define SC_AUTH_CHALLENGE_HEX_BUF_SIZE 33u

/** @brief Response MAC length in bytes (HMAC-SHA256 output). */
#define SC_AUTH_RESPONSE_BYTES SC_CRYPTO_SHA256_DIGEST_BYTES

/** @brief Buffer size for hex-encoded response MAC (64 hex + NUL). */
#define SC_AUTH_RESPONSE_HEX_BUF_SIZE SC_CRYPTO_SHA256_HEX_BUF_SIZE

/** @brief Pointer to the compile-time salt bytes. */
const uint8_t *sc_auth_salt(void);

/** @brief Length of the compile-time salt in bytes. */
size_t sc_auth_salt_len(void);

/**
 * @brief Derive the per-device authentication key from the device UID.
 *
 * @param uid     UID bytes (must not be NULL).
 * @param uid_len UID length in bytes.
 * @param out_key Output buffer of @ref SC_AUTH_KEY_BYTES bytes.
 * @return true on success, false on invalid args or crypto-backend failure.
 */
bool sc_auth_derive_device_key(
    const uint8_t *uid,
    size_t uid_len,
    uint8_t out_key[SC_AUTH_KEY_BYTES]);

/**
 * @brief Compute the expected challenge response.
 *
 * MAC = HMAC-SHA256(key=device_key,
 *                   message=challenge || session_id_be32)
 *
 * @param device_key    Per-device key from @ref sc_auth_derive_device_key.
 * @param challenge     Challenge nonce (must not be NULL).
 * @param challenge_len Challenge length (typically @ref SC_AUTH_CHALLENGE_BYTES).
 * @param session_id    Session id (must match the one returned by HELLO).
 * @param out_response  Output MAC of @ref SC_AUTH_RESPONSE_BYTES bytes.
 * @return true on success, false on invalid args or crypto-backend failure.
 */
bool sc_auth_compute_response(
    const uint8_t device_key[SC_AUTH_KEY_BYTES],
    const uint8_t *challenge,
    size_t challenge_len,
    uint32_t session_id,
    uint8_t out_response[SC_AUTH_RESPONSE_BYTES]);

/**
 * @brief Convenience wrapper that derives the key and computes the response
 *        in one call, then writes lowercase hex to @p out_hex.
 *
 * Equivalent to:
 *   sc_auth_derive_device_key(uid, uid_len, key);
 *   sc_auth_compute_response(key, challenge, challenge_len, session_id, mac);
 *   bytes_to_hex(mac, out_hex);
 *
 * @param uid             UID bytes.
 * @param uid_len         UID length in bytes.
 * @param challenge       Challenge nonce.
 * @param challenge_len   Challenge length.
 * @param session_id      Session id.
 * @param out_hex         Output buffer (at least
 *                        @ref SC_AUTH_RESPONSE_HEX_BUF_SIZE bytes).
 * @param out_hex_size    Size of @p out_hex in bytes.
 * @return true on success, false on invalid args / buffer / backend failure.
 */
bool sc_auth_compute_response_hex(
    const uint8_t *uid,
    size_t uid_len,
    const uint8_t *challenge,
    size_t challenge_len,
    uint32_t session_id,
    char *out_hex,
    size_t out_hex_size);

/**
 * @brief Decode a hex challenge string (e.g. from `SC_OK AUTH_CHALLENGE <hex>`)
 *        into its binary form.
 *
 * @param hex      NUL-terminated hex string (must be exactly @p out_len * 2 chars).
 * @param out      Output buffer.
 * @param out_len  Expected output length in bytes.
 * @return true on a clean parse, false otherwise (out is then untouched).
 */
bool sc_auth_decode_hex(const char *hex, uint8_t *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* SC_AUTH_H */
