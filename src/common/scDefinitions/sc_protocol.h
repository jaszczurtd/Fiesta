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
 * single-source-of-truth contract and is forbidden - see provider §11
 * Rule 5 (effective from refactor R1.1 onward).
 *
 * @note This header is intentionally HAL-free. The HAL-bound
 *       @c fiesta_default_vocabulary instance (passed to
 *       @c hal_serial_session_init_with_vocabulary) lives in
 *       @ref sc_session_vocabulary.h to keep the host build
 *       compilable on environments without JaszczurHAL on the
 *       include path (e.g. SerialConfigurator host CI runs that
 *       fall back to the @c sc_crypto_none backend).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Wire-shared sizing constants ──────────────────────────────────── */

/**
 * @brief Buffer size (with NUL terminator) for a parameter id on the wire.
 *
 * Both host and firmware MUST use this constant when sizing their
 * parameter-id buffers so the contract stays in one place. Strings on
 * the wire may be up to @c SC_PARAM_ID_MAX-1 bytes; longer payloads
 * are rejected by the firmware with @c SC_BAD_REQUEST param_id_too_long.
 */
#define SC_PARAM_ID_MAX 48u

/* ── Inbound command tokens (host -> device) ────────────────────────── */

#define SC_CMD_HELLO                "HELLO"
#define SC_CMD_GET_META             "SC_GET_META"
#define SC_CMD_GET_PARAM_LIST       "SC_GET_PARAM_LIST"
#define SC_CMD_GET_VALUES           "SC_GET_VALUES"
#define SC_CMD_GET_PARAM            "SC_GET_PARAM"   /**< prefix; followed by " <param_id>". */
#define SC_CMD_AUTH_BEGIN           "SC_AUTH_BEGIN"
#define SC_CMD_AUTH_PROVE           "SC_AUTH_PROVE"  /**< prefix; followed by " <hex>". */
#define SC_CMD_REBOOT_BOOTLOADER    "SC_REBOOT_BOOTLOADER"

/* Phase 8 — auth-gated parameter staging. SET_PARAM mutates a staging
 * mirror only; COMMIT_PARAMS validates cross-field rules and persists
 * the active blob; REVERT_PARAMS resets staging from active. */
#define SC_CMD_SET_PARAM            "SC_SET_PARAM"   /**< prefix; followed by " <param_id> <value>". */
#define SC_CMD_COMMIT_PARAMS        "SC_COMMIT_PARAMS"
#define SC_CMD_REVERT_PARAMS        "SC_REVERT_PARAMS"

/* ── Outbound reply status tokens (device -> host) ──────────────────── */

#define SC_STATUS_OK                "SC_OK"
#define SC_STATUS_UNKNOWN_CMD       "SC_UNKNOWN_CMD"
#define SC_STATUS_BAD_REQUEST       "SC_BAD_REQUEST"
#define SC_STATUS_INVALID_PARAM_ID  "SC_INVALID_PARAM_ID"
#define SC_STATUS_NOT_READY         "SC_NOT_READY"
#define SC_STATUS_NOT_AUTHORIZED    "SC_NOT_AUTHORIZED"
#define SC_STATUS_AUTH_FAILED       "SC_AUTH_FAILED"
#define SC_STATUS_COMMIT_FAILED     "SC_COMMIT_FAILED" /**< Phase 8: cross-field rule violation at COMMIT. */

/* ── Reply sub-tokens ──────────────────────────────────────────────── */

#define SC_REPLY_TAG_HELLO_REQUIRED        "HELLO_REQUIRED"

#define SC_REPLY_TAG_META                  "META"
#define SC_REPLY_TAG_PARAM_LIST            "PARAM_LIST"
#define SC_REPLY_TAG_PARAM_VALUES          "PARAM_VALUES"
#define SC_REPLY_TAG_PARAM                 "PARAM"
#define SC_REPLY_TAG_AUTH_CHALLENGE        "AUTH_CHALLENGE"
#define SC_REPLY_TAG_AUTH_OK               "AUTH_OK"
#define SC_REPLY_TAG_REBOOT                "REBOOT"

#define SC_REPLY_TAG_PARAM_SET             "PARAM_SET"
#define SC_REPLY_TAG_PARAMS_COMMITTED      "PARAMS_COMMITTED"
#define SC_REPLY_TAG_PARAMS_REVERTED       "PARAMS_REVERTED"

/* Structural HELLO reply head - emitted by the HAL session helper as
 * "OK HELLO module=... proto=... session=... fw=... build=... uid=..."
 * Hosts parse the leading "OK HELLO" via strncmp. The "OK " prefix
 * here is intentionally NOT SC_STATUS_OK (which is "SC_OK") because
 * the HELLO reply predates the SC_OK convention and stays as plain
 * "OK" for protocol compatibility (see also HAL session helper). */
#define SC_REPLY_HELLO_HEAD                "OK HELLO"

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

/* "SC_OK PARAM id=<id> value=<int> min=<int> max=<int> default=<int>
 *  group=<snake_case>"
 * The group token carries the descriptor's UI section (snake_case, no
 * spaces). Empty group is rendered as `group=` (zero-length value);
 * unknown keys are silently ignored by the host parser, so older host
 * builds remain compatible. */
#define SC_REPLY_PARAM_FMT                                                   \
    SC_STATUS_OK " " SC_REPLY_TAG_PARAM                                      \
    " id=%s value=%d min=%d max=%d default=%d group=%s"

/* "SC_INVALID_PARAM_ID id=<id>" */
#define SC_REPLY_INVALID_PARAM_ID_FMT                                        \
    SC_STATUS_INVALID_PARAM_ID " id=%s"

/* "SC_BAD_REQUEST expected=<expected>" */
#define SC_REPLY_BAD_REQUEST_EXPECTED_FMT                                    \
    SC_STATUS_BAD_REQUEST " expected=%s"

/* "SC_OK AUTH_CHALLENGE <hex>" - emitted by HAL session helper. */
#define SC_REPLY_AUTH_CHALLENGE_FMT                                          \
    SC_STATUS_OK " " SC_REPLY_TAG_AUTH_CHALLENGE " %s"

/* "SC_OK AUTH_OK" */
#define SC_REPLY_AUTH_OK                                                     \
    SC_STATUS_OK " " SC_REPLY_TAG_AUTH_OK

/* "SC_OK REBOOT" */
#define SC_REPLY_REBOOT_OK                                                   \
    SC_STATUS_OK " " SC_REPLY_TAG_REBOOT

/* "SC_AUTH_FAILED <reason>" - reasons that the HAL session helper emits. */
#define SC_REPLY_AUTH_FAILED_NO_CHALLENGE   SC_STATUS_AUTH_FAILED " no_challenge"
#define SC_REPLY_AUTH_FAILED_BAD_LENGTH     SC_STATUS_AUTH_FAILED " bad_length"
#define SC_REPLY_AUTH_FAILED_BAD_HEX        SC_STATUS_AUTH_FAILED " bad_hex"
#define SC_REPLY_AUTH_FAILED_KEY_DERIVATION SC_STATUS_AUTH_FAILED " key_derivation"
#define SC_REPLY_AUTH_FAILED_MAC_COMPUTE    SC_STATUS_AUTH_FAILED " mac_compute"
#define SC_REPLY_AUTH_FAILED_BAD_MAC        SC_STATUS_AUTH_FAILED " bad_mac"

/* "SC_NOT_READY HELLO_REQUIRED" */
#define SC_REPLY_NOT_READY_HELLO_REQUIRED                                    \
    SC_STATUS_NOT_READY " " SC_REPLY_TAG_HELLO_REQUIRED

/* ── Phase 8 — parameter staging reply formats ─────────────────────── */

/* "SC_OK PARAM_SET id=<id> staged=<int> active=<int>" — emitted by
 * the generic write helper after a SET_PARAM that passes RO + range
 * validation. Both staged and active are reported so the host can
 * confirm the staging slot moved while the active mirror stayed put. */
#define SC_REPLY_PARAM_SET_FMT                                               \
    SC_STATUS_OK " " SC_REPLY_TAG_PARAM_SET                                  \
    " id=%s staged=%d active=%d"

/* "SC_OK PARAMS_COMMITTED count=<n>" — emitted by COMMIT_PARAMS after
 * staging->active copy + persist. <n> is the number of writable scalar
 * descriptors copied (RO descriptors are skipped, not counted). */
#define SC_REPLY_PARAMS_COMMITTED_FMT                                        \
    SC_STATUS_OK " " SC_REPLY_TAG_PARAMS_COMMITTED                           \
    " count=%u"

/* "SC_OK PARAMS_REVERTED" — emitted by REVERT_PARAMS after staging is
 * reset from the active mirror. No body — revert is unconditional. */
#define SC_REPLY_PARAMS_REVERTED                                             \
    SC_STATUS_OK " " SC_REPLY_TAG_PARAMS_REVERTED

/* "SC_COMMIT_FAILED reason=<token>" — emitted by COMMIT_PARAMS when
 * cross-field validation rejects the staged blob (heater_vs_fan_order,
 * fan_coolant_hysteresis, ...). Active blob and persisted state are
 * untouched on this path. */
#define SC_REPLY_COMMIT_FAILED_FMT                                           \
    SC_STATUS_COMMIT_FAILED " reason=%s"

/* "SC_BAD_REQUEST read_only id=<id>" — SET_PARAM on a descriptor
 * carrying SC_PARAM_FLAG_READ_ONLY. */
#define SC_REPLY_BAD_REQUEST_READ_ONLY_FMT                                   \
    SC_STATUS_BAD_REQUEST " read_only id=%s"

/* "SC_BAD_REQUEST out_of_range id=<id> min=<n> max=<n>" — SET_PARAM
 * value outside the descriptor's declared [min, max]. */
#define SC_REPLY_BAD_REQUEST_OUT_OF_RANGE_FMT                                \
    SC_STATUS_BAD_REQUEST " out_of_range id=%s min=%d max=%d"

#ifdef __cplusplus
}
#endif
