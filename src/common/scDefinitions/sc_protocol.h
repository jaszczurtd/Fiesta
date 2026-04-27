#pragma once

/**
 * @file sc_protocol.h
 * @brief Single source of truth for the SerialConfigurator wire protocol
 *        vocabulary used by Fiesta firmware modules and by the host tool.
 *
 * Every SC_* command and status token used anywhere in firmware
 * (ECU, Clocks, OilAndSpeed) or host (SerialConfigurator core / CLI / UI)
 * MUST live here. Adding a raw "SC_..." literal directly in module
 * config.c, host transport, or CLI handlers bypasses the
 * single-source-of-truth contract and is forbidden — see provider §11
 * Rule 5 (effective from refactor R1.1 onward).
 *
 * Reply format strings and the canonical Fiesta vocabulary instance
 * (passed to @c hal_serial_session_init_with_vocabulary) also live
 * here. JaszczurHAL itself does NOT include this header — the helper
 * receives the dialect via the init pointer.
 */

#include "hal/hal_serial_session_vocabulary.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Inbound command tokens (host → device) ────────────────────────── */

#define SC_CMD_HELLO                "HELLO"
#define SC_CMD_GET_META             "SC_GET_META"
#define SC_CMD_GET_PARAM_LIST       "SC_GET_PARAM_LIST"
#define SC_CMD_GET_VALUES           "SC_GET_VALUES"
#define SC_CMD_GET_PARAM            "SC_GET_PARAM"   /**< prefix; followed by " <param_id>". */
#define SC_CMD_AUTH_BEGIN           "SC_AUTH_BEGIN"
#define SC_CMD_AUTH_PROVE           "SC_AUTH_PROVE"  /**< prefix; followed by " <hex>". */
#define SC_CMD_REBOOT_BOOTLOADER    "SC_REBOOT_BOOTLOADER"

/* ── Outbound reply status tokens (device → host) ──────────────────── */

#define SC_STATUS_OK                "SC_OK"
#define SC_STATUS_UNKNOWN_CMD       "SC_UNKNOWN_CMD"
#define SC_STATUS_BAD_REQUEST       "SC_BAD_REQUEST"
#define SC_STATUS_INVALID_PARAM_ID  "SC_INVALID_PARAM_ID"
#define SC_STATUS_NOT_READY         "SC_NOT_READY"
#define SC_STATUS_NOT_AUTHORIZED    "SC_NOT_AUTHORIZED"
#define SC_STATUS_AUTH_FAILED       "SC_AUTH_FAILED"

/* ── Reply sub-tokens ──────────────────────────────────────────────── */

#define SC_REPLY_TAG_HELLO_REQUIRED        "HELLO_REQUIRED"

#define SC_REPLY_TAG_META                  "META"
#define SC_REPLY_TAG_PARAM_LIST            "PARAM_LIST"
#define SC_REPLY_TAG_PARAM_VALUES          "PARAM_VALUES"
#define SC_REPLY_TAG_PARAM                 "PARAM"
#define SC_REPLY_TAG_AUTH_CHALLENGE        "AUTH_CHALLENGE"
#define SC_REPLY_TAG_AUTH_OK               "AUTH_OK"
#define SC_REPLY_TAG_REBOOT                "REBOOT"

/* ── Reply format strings (compose with snprintf) ──────────────────── */

/* "SC_OK META module=<name> proto=<n> session=<id> fw=<ver> build=<b64> uid=<hex>" */
#define SC_REPLY_META_FMT                                                    \
    SC_STATUS_OK " " SC_REPLY_TAG_META                                       \
    " module=%s proto=%u session=%lu fw=%s build=%s uid=%s"

/* Head of the param-list reply; per-id chunks are appended by the helper. */
#define SC_REPLY_PARAM_LIST_HEAD                                             \
    SC_STATUS_OK " " SC_REPLY_TAG_PARAM_LIST

/* Head of the param-values reply; "<id>=<value>" chunks appended by helper. */
#define SC_REPLY_PARAM_VALUES_HEAD                                           \
    SC_STATUS_OK " " SC_REPLY_TAG_PARAM_VALUES

/* "SC_OK PARAM id=<id> value=<int> min=<int> max=<int> default=<int>" */
#define SC_REPLY_PARAM_FMT                                                   \
    SC_STATUS_OK " " SC_REPLY_TAG_PARAM                                      \
    " id=%s value=%d min=%d max=%d default=%d"

/* "SC_INVALID_PARAM_ID id=<id>" */
#define SC_REPLY_INVALID_PARAM_ID_FMT                                        \
    SC_STATUS_INVALID_PARAM_ID " id=%s"

/* "SC_BAD_REQUEST expected=<expected>" */
#define SC_REPLY_BAD_REQUEST_EXPECTED_FMT                                    \
    SC_STATUS_BAD_REQUEST " expected=%s"

/* "SC_OK AUTH_CHALLENGE <hex>" — emitted by HAL session helper. */
#define SC_REPLY_AUTH_CHALLENGE_FMT                                          \
    SC_STATUS_OK " " SC_REPLY_TAG_AUTH_CHALLENGE " %s"

/* "SC_OK AUTH_OK" */
#define SC_REPLY_AUTH_OK                                                     \
    SC_STATUS_OK " " SC_REPLY_TAG_AUTH_OK

/* "SC_OK REBOOT" */
#define SC_REPLY_REBOOT_OK                                                   \
    SC_STATUS_OK " " SC_REPLY_TAG_REBOOT

/* "SC_AUTH_FAILED <reason>" — reasons that the HAL session helper emits. */
#define SC_REPLY_AUTH_FAILED_NO_CHALLENGE   SC_STATUS_AUTH_FAILED " no_challenge"
#define SC_REPLY_AUTH_FAILED_BAD_LENGTH     SC_STATUS_AUTH_FAILED " bad_length"
#define SC_REPLY_AUTH_FAILED_BAD_HEX        SC_STATUS_AUTH_FAILED " bad_hex"
#define SC_REPLY_AUTH_FAILED_KEY_DERIVATION SC_STATUS_AUTH_FAILED " key_derivation"
#define SC_REPLY_AUTH_FAILED_MAC_COMPUTE    SC_STATUS_AUTH_FAILED " mac_compute"
#define SC_REPLY_AUTH_FAILED_BAD_MAC        SC_STATUS_AUTH_FAILED " bad_mac"

/* "SC_NOT_READY HELLO_REQUIRED" */
#define SC_REPLY_NOT_READY_HELLO_REQUIRED                                    \
    SC_STATUS_NOT_READY " " SC_REPLY_TAG_HELLO_REQUIRED

/* ── Fiesta default vocabulary instance ─────────────────────────────── */

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
