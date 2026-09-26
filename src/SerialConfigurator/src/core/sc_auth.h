#ifndef SC_AUTH_H
#define SC_AUTH_H

/*
 * Host-side helpers for the SerialConfigurator authentication handshake.
 *
 * The salt, the per-device key derivation and the challenge response are
 * the firmware's own code: hal_sc_auth from JaszczurHAL, compiled into
 * serial_configurator_core. Callers use the HAL_SC_AUTH_* constants,
 * hal_sc_auth_derive_device_key() and hal_sc_auth_compute_response()
 * directly. This header adds only what the host needs on top: decoding of
 * hex wire fields and a one-call hex response.
 */

#include <hal/security/hal_sc_auth.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Derive the device key from @p uid and answer @p challenge in one
 *        call, writing the MAC as lowercase hex.
 *
 * Runs hal_sc_auth_derive_device_key() and hal_sc_auth_compute_response()
 * back to back and wipes the derived key before returning.
 *
 * @param uid           UID bytes reported by HELLO (must not be NULL).
 * @param uid_len       UID length in bytes.
 * @param challenge     Challenge nonce from SC_AUTH_BEGIN (must not be NULL).
 * @param challenge_len Challenge length, at most HAL_SC_AUTH_CHALLENGE_BYTES.
 * @param session_id    Session id echoed by HELLO.
 * @param out_hex       Output buffer of at least
 *                      HAL_SC_AUTH_RESPONSE_HEX_BUF_SIZE bytes.
 * @param out_hex_size  Size of @p out_hex in bytes.
 * @return true on success; false on invalid arguments or a too small buffer,
 *         with @p out_hex set to an empty string when it can hold one.
 */
bool sc_auth_compute_response_hex(const uint8_t *uid, size_t uid_len,
                                  const uint8_t *challenge,
                                  size_t challenge_len, uint32_t session_id,
                                  char *out_hex, size_t out_hex_size);

/**
 * @brief Decode a hex wire field (UID, challenge) into bytes.
 *
 * @param hex     NUL-terminated string of exactly @p out_len * 2 hex digits,
 *                either case.
 * @param out     Output buffer of @p out_len bytes; may hold a partial result
 *                when decoding fails.
 * @param out_len Expected number of decoded bytes.
 * @return true on a clean parse; false for NULL arguments, a non-hex digit,
 *         a shorter string or trailing characters.
 */
bool sc_auth_decode_hex(const char *hex, uint8_t *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* SC_AUTH_H */
