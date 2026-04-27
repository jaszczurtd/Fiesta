#pragma once

/**
 * @file sc_session_vocabulary.h
 * @brief HAL-bound binding between Fiesta SC tokens and the
 *        @ref hal_serial_session_vocabulary_t struct expected by
 *        @c hal_serial_session_init_with_vocabulary.
 *
 * Splitting this off from @ref sc_protocol.h keeps the wire-token
 * SSOT compilable on environments that do not have JaszczurHAL on
 * the include path (e.g. SerialConfigurator host CI runs that fall
 * back to the @c sc_crypto_none backend). Firmware modules
 * (ECU, Clocks, OilAndSpeed) include this file because they own a
 * @c hal_serial_session_t and must hand the dialect to the HAL
 * helper at init; pure host tooling (SC core / CLI / UI) only
 * needs @ref sc_protocol.h.
 */

#include "sc_protocol.h"

#include "hal/hal_serial_session_vocabulary.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Canonical Fiesta vocabulary instance.
 *
 * Pass to @c hal_serial_session_init_with_vocabulary() so the HAL
 * session helper speaks the Fiesta dialect. The contents currently
 * match @c hal_serial_session_vocabulary_default verbatim — Fiesta
 * tokens are byte-identical to the legacy HAL defaults today. Keeping
 * the instance here is the single-source-of-truth: if Fiesta later
 * renames a token, only this instance changes; the HAL default stays
 * a development fallback.
 *
 * The HAL helper itself does NOT include this header — values flow in
 * via the init pointer (R1.0 vocabulary decoupling).
 */
static const hal_serial_session_vocabulary_t fiesta_default_vocabulary = {
    .cmd_auth_begin = SC_CMD_AUTH_BEGIN,
    .cmd_auth_prove = SC_CMD_AUTH_PROVE,
    .cmd_reboot_bootloader = SC_CMD_REBOOT_BOOTLOADER,
    .reply_unknown_cmd = SC_STATUS_UNKNOWN_CMD,
    .reply_not_ready_hello_required = SC_REPLY_NOT_READY_HELLO_REQUIRED,
    .reply_auth_challenge_fmt = SC_REPLY_AUTH_CHALLENGE_FMT,
    .reply_auth_ok = SC_REPLY_AUTH_OK,
    .reply_auth_failed_no_challenge = SC_REPLY_AUTH_FAILED_NO_CHALLENGE,
    .reply_auth_failed_bad_length = SC_REPLY_AUTH_FAILED_BAD_LENGTH,
    .reply_auth_failed_bad_hex = SC_REPLY_AUTH_FAILED_BAD_HEX,
    .reply_auth_failed_key_derivation = SC_REPLY_AUTH_FAILED_KEY_DERIVATION,
    .reply_auth_failed_mac_compute = SC_REPLY_AUTH_FAILED_MAC_COMPUTE,
    .reply_auth_failed_bad_mac = SC_REPLY_AUTH_FAILED_BAD_MAC,
    .reply_not_authorized = SC_STATUS_NOT_AUTHORIZED,
    .reply_reboot_ok = SC_REPLY_REBOOT_OK,
};

#ifdef __cplusplus
}
#endif
