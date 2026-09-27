#pragma once

/**
 * @file sc_protocol.h
 * @brief Single source of truth for the SerialConfigurator wire protocol
 *        vocabulary used by Fiesta firmware modules and by the host tool.
 *
 * Every SC_* command and status token used anywhere in firmware
 * (ECU, Clocks, OilAndSpeed) or host (SerialConfigurator core / CLI / UI)
 * MUST live here. Adding a raw "SC_..." literal directly in module
 * config.c, host transport, or CLI handlers bypasses this shared
 * definition and is forbidden - see provider §11
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
 * parameter-id buffers so the limit stays in one place. Strings on
 * the wire may be up to @c SC_PARAM_ID_MAX-1 bytes; longer payloads
 * are rejected by the firmware with @c SC_BAD_REQUEST param_id_too_long.
 */
#define SC_PARAM_ID_MAX 48u

/* ── Inbound command tokens (host -> device) ────────────────────────── */

#define SC_COMMAND_PREFIX "SC_"
#define SC_CMD_HELLO "HELLO"
#define SC_CMD_BYE "SC_BYE"
#define SC_CMD_GET_META "SC_GET_META"
#define SC_CMD_GET_PARAM_LIST "SC_GET_PARAM_LIST"
#define SC_CMD_GET_VALUES "SC_GET_VALUES"
#define SC_CMD_GET_PARAM                                                       \
  "SC_GET_PARAM" /**< prefix; followed by " <param_id>". */
#define SC_CMD_AUTH_BEGIN "SC_AUTH_BEGIN"
#define SC_CMD_AUTH_PROVE                                                      \
  "SC_AUTH_PROVE" /**< prefix; followed by " <hex>".                           \
                   */
#define SC_CMD_REBOOT_BOOTLOADER "SC_REBOOT_BOOTLOADER"

/* Phase 8 - auth-gated parameter staging. SET_PARAM mutates a staging
 * mirror only; COMMIT_PARAMS validates cross-field rules and persists
 * the active blob; REVERT_PARAMS resets staging from active. */
#define SC_CMD_SET_PARAM                                                       \
  "SC_SET_PARAM" /**< prefix; followed by " <param_id> <value>". */
#define SC_CMD_COMMIT_PARAMS "SC_COMMIT_PARAMS"
#define SC_CMD_REVERT_PARAMS "SC_REVERT_PARAMS"

/* Read-only telemetry endpoint outside the descriptor framework
 * (sibling to SC_GET_META). Returns an atomic GPS snapshot (lat/lon
 * in microdegrees, speed in 0.1 km/h units, Unix epoch and an
 * availability flag). Not auth-gated, not persisted, never staged. */
#define SC_CMD_GET_GPS "SC_GET_GPS"

/* Functional tests of a bench build. Reads describe the catalog and the
 * running test; SET/RUN/STOP/SKIP need an authenticated session. A test
 * started over this path belongs to the session: the module stops it when
 * the session ends or the host stops polling SC_TEST_STATUS. */
#define SC_CMD_TEST_LIST "SC_TEST_LIST"
#define SC_CMD_TEST_INFO "SC_TEST_INFO"   /**< prefix; followed by " <test>". */
#define SC_CMD_TEST_PARAM "SC_TEST_PARAM" /**< prefix; followed by " <id>". */
#define SC_CMD_TEST_SET                                                        \
  "SC_TEST_SET" /**< prefix; followed by " <param_id> <value>". */
#define SC_CMD_TEST_RUN                                                        \
  "SC_TEST_RUN" /**< prefix; followed by " <test>" or " all". */
#define SC_CMD_TEST_STOP "SC_TEST_STOP"
#define SC_CMD_TEST_SKIP "SC_TEST_SKIP"
#define SC_CMD_TEST_STATUS "SC_TEST_STATUS"
/** @brief Argument of SC_TEST_RUN that starts the sequence of listed tests. */
#define SC_TEST_SEQUENCE "all"
/** @brief Longest test name or test parameter id on the wire, with NUL. */
#define SC_TEST_ID_MAX 24u

/* Names the firmware registry gives its configurator tests and the host
 * translates. */
#define SC_TEST_NAME_CYCLIC "cyclic"
#define SC_TEST_NAME_RANDOM "random"
#define SC_TEST_NAME_TOP "top"
#define SC_TEST_NAME_TOPZERO "topzero"
#define SC_TEST_NAME_POT "pot"

/* Runtime parameter ids; unique across tests. */
#define SC_TEST_PARAM_CYCLIC_PASSES "cyclic_passes"
#define SC_TEST_PARAM_CYCLIC_CYCLES "cyclic_cycles"
#define SC_TEST_PARAM_RANDOM_DURATION "random_duration"
#define SC_TEST_PARAM_RANDOM_HOLD "random_hold"
#define SC_TEST_PARAM_TOP_DWELL "top_dwell"
#define SC_TEST_PARAM_TOP_SERIES "top_series"
#define SC_TEST_PARAM_TOPZERO_DWELL "topzero_dwell"
#define SC_TEST_PARAM_TOPZERO_SERIES "topzero_series"
#define SC_TEST_PARAM_POT_RATE "pot_rate"
#define SC_TEST_PARAM_POT_HOLD "pot_hold"
#define SC_TEST_PARAM_POT_REST "pot_rest"
#define SC_TEST_PARAM_POT_PASSES "pot_passes"

/* Parameter unit tokens. */
#define SC_TEST_UNIT_COUNT "count"
#define SC_TEST_UNIT_S "s"
#define SC_TEST_UNIT_MS "ms"
#define SC_TEST_UNIT_PCT_PER_S "pct_per_s"

/* SC_TEST_STATUS keys and their fixed values. */
#define SC_TEST_KEY_STATE "state"
#define SC_TEST_KEY_RUNS "runs" /**< Tests started since boot. */
#define SC_TEST_KEY_TEST "test"
#define SC_TEST_KEY_SOURCE "src"
#define SC_TEST_KEY_SEQ "seq"
#define SC_TEST_KEY_ELAPSED_MS "elapsed_ms"
#define SC_TEST_KEY_DEMAND_X10 "demand_x10"
#define SC_TEST_KEY_POSITION_X10 "position_x10"
#define SC_TEST_KEY_LAST "last"
#define SC_TEST_KEY_RESULT "result"
#define SC_TEST_STATE_IDLE "idle"
#define SC_TEST_STATE_RUNNING "running"
#define SC_TEST_SOURCE_SC "sc"
#define SC_TEST_SOURCE_CONSOLE "console"

/* Progress keys of the running test. */
#define SC_TEST_FIELD_PROFILE "profile" /**< cyclic: speed profile, 1..4. */
#define SC_TEST_FIELD_RATE "rate"       /**< demand rate [%/s]. */
#define SC_TEST_FIELD_CYCLE "cycle"     /**< cyclic: cycle in the profile. */
#define SC_TEST_FIELD_CYCLES "cycles"   /**< cyclic: cycles per profile. */
#define SC_TEST_FIELD_PASS "pass"       /**< pass in progress, 1-based. */
#define SC_TEST_FIELD_PASSES "passes"   /**< passes in the test. */
#define SC_TEST_FIELD_TARGET "target"   /**< random: drawn position [%]. */
#define SC_TEST_FIELD_HOLD_LEFT_MS                                             \
  "hold_left_ms"                      /**< random: time to redraw. */
#define SC_TEST_FIELD_LEFT_S "left_s" /**< random: time to the end [s]. */
#define SC_TEST_FIELD_SETPOINT_X10 "setpoint_x10" /**< top: step [% x10]. */
#define SC_TEST_FIELD_SERIES "series" /**< top: series in progress. */
#define SC_TEST_FIELD_SERIES_COUNT "series_count"
#define SC_TEST_FIELD_PHASE "phase" /**< pot: one of SC_TEST_PHASE_*. */
#define SC_TEST_PHASE_RISE "rise"
#define SC_TEST_PHASE_HOLD "hold"
#define SC_TEST_PHASE_FALL "fall"
#define SC_TEST_PHASE_REST "rest"

/* Result of the most recent finished test. */
#define SC_TEST_RESULT_DONE "done"
#define SC_TEST_RESULT_OK "ok"
#define SC_TEST_RESULT_FAILED "failed"
#define SC_TEST_RESULT_STOPPED "stopped"
#define SC_TEST_RESULT_HOST_LOST "host_lost"
#define SC_TEST_RESULT_SESSION_END "session_end"
#define SC_TEST_RESULT_ENGINE_RUNNING "engine_running"

/* ── Outbound reply status tokens (device -> host) ──────────────────── */

#define SC_STATUS_OK "SC_OK"
#define SC_STATUS_UNKNOWN_CMD "SC_UNKNOWN_CMD"
#define SC_STATUS_BAD_REQUEST "SC_BAD_REQUEST"
#define SC_STATUS_INVALID_PARAM_ID "SC_INVALID_PARAM_ID"
#define SC_STATUS_NOT_READY "SC_NOT_READY"
#define SC_STATUS_NOT_AUTHORIZED "SC_NOT_AUTHORIZED"
#define SC_STATUS_AUTH_FAILED "SC_AUTH_FAILED"
#define SC_STATUS_COMMIT_FAILED                                                \
  "SC_COMMIT_FAILED" /**< Phase 8: cross-field rule violation at COMMIT. */

/* ── Reply sub-tokens ──────────────────────────────────────────────── */

#define SC_REPLY_TAG_HELLO_REQUIRED "HELLO_REQUIRED"
#define SC_REPLY_TAG_STORAGE_RECOVERY "STORAGE_RECOVERY"

#define SC_REPLY_TAG_META "META"
#define SC_REPLY_TAG_BYE "BYE"
#define SC_REPLY_TAG_PARAM_LIST "PARAM_LIST"
#define SC_REPLY_TAG_PARAM_VALUES "PARAM_VALUES"
#define SC_REPLY_TAG_PARAM "PARAM"
#define SC_REPLY_TAG_AUTH_CHALLENGE "AUTH_CHALLENGE"
#define SC_REPLY_TAG_AUTH_OK "AUTH_OK"
#define SC_REPLY_TAG_REBOOT "REBOOT"

#define SC_REPLY_TAG_PARAM_SET "PARAM_SET"
#define SC_REPLY_TAG_PARAMS_COMMITTED "PARAMS_COMMITTED"
#define SC_REPLY_TAG_PARAMS_REVERTED "PARAMS_REVERTED"

#define SC_REPLY_TAG_GPS "GPS"

#define SC_REPLY_TAG_TEST_LIST "TEST_LIST"
#define SC_REPLY_TAG_TEST_INFO "TEST_INFO"
#define SC_REPLY_TAG_TEST_PARAM "TEST_PARAM"
#define SC_REPLY_TAG_TEST_SET "TEST_SET"
#define SC_REPLY_TAG_TEST_RUN "TEST_RUN"
#define SC_REPLY_TAG_TEST_STOP "TEST_STOP"
#define SC_REPLY_TAG_TEST_SKIP "TEST_SKIP"
#define SC_REPLY_TAG_TEST_STATUS "TEST_STATUS"
#define SC_REPLY_TAG_ENGINE_RUNNING "ENGINE_RUNNING"
#define SC_REPLY_TAG_BUSY "BUSY"
/* Reasons after SC_BAD_REQUEST that a host tells apart. */
#define SC_REPLY_REASON_READ_ONLY "read_only"
#define SC_REPLY_REASON_OUT_OF_RANGE "out_of_range"
#define SC_REPLY_REASON_UNKNOWN_TEST "unknown_test"

/* Structural HELLO reply head - emitted by the HAL session helper as
 * "OK HELLO module=... proto=... session=... fw=... build=... uid=..."
 * Hosts parse the leading "OK HELLO" via strncmp. The "OK " prefix
 * here is intentionally NOT SC_STATUS_OK (which is "SC_OK") because
 * the HELLO reply predates the SC_OK convention and stays as plain
 * "OK" for protocol compatibility (see also HAL session helper). */
#define SC_REPLY_HELLO_HEAD "OK HELLO"

/* ── Reply format strings (compose with snprintf) ──────────────────── */

/* "SC_OK META module=<name> proto=<n> session=<id> fw=<ver> build=<b64>
 * uid=<hex>" */
#define SC_REPLY_META_FMT                                                      \
  SC_STATUS_OK " " SC_REPLY_TAG_META                                           \
               " module=%s proto=%u session=%lu fw=%s build=%s uid=%s"

/* Head of the param-list reply; per-id chunks are appended by the helper. */
#define SC_REPLY_PARAM_LIST_HEAD SC_STATUS_OK " " SC_REPLY_TAG_PARAM_LIST

/* Head of the param-values reply; "<id>=<value>" chunks appended by helper. */
#define SC_REPLY_PARAM_VALUES_HEAD SC_STATUS_OK " " SC_REPLY_TAG_PARAM_VALUES

/* "SC_OK PARAM id=<id> value=<int> min=<int> max=<int> default=<int>
 *  group=<snake_case>"
 * The group token carries the descriptor's UI section (snake_case, no
 * spaces). Empty group is rendered as `group=` (zero-length value);
 * unknown keys are silently ignored by the host parser, so older host
 * builds remain compatible. */
#define SC_REPLY_PARAM_FMT                                                     \
  SC_STATUS_OK " " SC_REPLY_TAG_PARAM                                          \
               " id=%s value=%d min=%d max=%d default=%d group=%s"

/* "SC_INVALID_PARAM_ID id=<id>" */
#define SC_REPLY_INVALID_PARAM_ID_FMT SC_STATUS_INVALID_PARAM_ID " id=%s"

/* "SC_BAD_REQUEST expected=<expected>" */
#define SC_REPLY_BAD_REQUEST_EXPECTED_FMT SC_STATUS_BAD_REQUEST " expected=%s"

/* "SC_OK AUTH_CHALLENGE <hex>" - emitted by HAL session helper. */
#define SC_REPLY_AUTH_CHALLENGE_FMT                                            \
  SC_STATUS_OK " " SC_REPLY_TAG_AUTH_CHALLENGE " %s"

/* "SC_OK AUTH_OK" */
#define SC_REPLY_AUTH_OK SC_STATUS_OK " " SC_REPLY_TAG_AUTH_OK

/* "SC_OK REBOOT" */
#define SC_REPLY_REBOOT_OK SC_STATUS_OK " " SC_REPLY_TAG_REBOOT

/* "SC_AUTH_FAILED <reason>" - reasons that the HAL session helper emits. */
#define SC_REPLY_AUTH_FAILED_NO_CHALLENGE SC_STATUS_AUTH_FAILED " no_challenge"
#define SC_REPLY_AUTH_FAILED_BAD_LENGTH SC_STATUS_AUTH_FAILED " bad_length"
#define SC_REPLY_AUTH_FAILED_BAD_HEX SC_STATUS_AUTH_FAILED " bad_hex"
#define SC_REPLY_AUTH_FAILED_KEY_DERIVATION                                    \
  SC_STATUS_AUTH_FAILED " key_derivation"
#define SC_REPLY_AUTH_FAILED_MAC_COMPUTE SC_STATUS_AUTH_FAILED " mac_compute"
#define SC_REPLY_AUTH_FAILED_BAD_MAC SC_STATUS_AUTH_FAILED " bad_mac"
#define SC_REPLY_AUTH_FAILED_ENTROPY SC_STATUS_AUTH_FAILED " entropy"

/* "SC_NOT_READY HELLO_REQUIRED" */
#define SC_REPLY_NOT_READY_HELLO_REQUIRED                                      \
  SC_STATUS_NOT_READY " " SC_REPLY_TAG_HELLO_REQUIRED

/* "SC_NOT_READY STORAGE_RECOVERY" */
#define SC_REPLY_NOT_READY_STORAGE_RECOVERY                                    \
  SC_STATUS_NOT_READY " " SC_REPLY_TAG_STORAGE_RECOVERY

/* "SC_OK BYE" */
#define SC_REPLY_BYE_OK SC_STATUS_OK " " SC_REPLY_TAG_BYE

/* ── Phase 8 - parameter staging reply formats ─────────────────────── */

/* "SC_OK PARAM_SET id=<id> staged=<int> active=<int>" - emitted by
 * the generic write helper after a SET_PARAM that passes RO + range
 * validation. Both staged and active are reported so the host can
 * confirm the staging slot moved while the active mirror stayed put. */
#define SC_REPLY_PARAM_SET_FMT                                                 \
  SC_STATUS_OK " " SC_REPLY_TAG_PARAM_SET " id=%s staged=%d active=%d"

/* "SC_OK PARAMS_COMMITTED count=<n>" - emitted by COMMIT_PARAMS after
 * staging->active copy + persist. <n> is the number of writable scalar
 * descriptors copied (RO descriptors are skipped, not counted). */
#define SC_REPLY_PARAMS_COMMITTED_FMT                                          \
  SC_STATUS_OK " " SC_REPLY_TAG_PARAMS_COMMITTED " count=%u"

/* "SC_OK PARAMS_REVERTED" - emitted by REVERT_PARAMS after staging is
 * reset from the active mirror. No body - revert is unconditional. */
#define SC_REPLY_PARAMS_REVERTED SC_STATUS_OK " " SC_REPLY_TAG_PARAMS_REVERTED

/* "SC_COMMIT_FAILED reason=<token>" - emitted by COMMIT_PARAMS when
 * cross-field validation rejects the staged blob (heater_vs_fan_order,
 * fan_coolant_hysteresis, ...). Active blob and persisted state are
 * untouched on this path. */
#define SC_REPLY_COMMIT_FAILED_FMT SC_STATUS_COMMIT_FAILED " reason=%s"

/* "SC_BAD_REQUEST read_only id=<id>" - SET_PARAM on a descriptor
 * carrying SC_PARAM_FLAG_READ_ONLY. */
#define SC_REPLY_BAD_REQUEST_READ_ONLY_FMT                                     \
  SC_STATUS_BAD_REQUEST " " SC_REPLY_REASON_READ_ONLY " id=%s"

/* "SC_BAD_REQUEST out_of_range id=<id> min=<n> max=<n>" - SET_PARAM
 * value outside the descriptor's declared [min, max]. */
#define SC_REPLY_BAD_REQUEST_OUT_OF_RANGE_FMT                                  \
  SC_STATUS_BAD_REQUEST " " SC_REPLY_REASON_OUT_OF_RANGE " id=%s min=%d "      \
                                                         "max=%d"

/* ── GPS telemetry snapshot reply ──────────────────────────────────── */

/* "SC_OK GPS available=<0|1> lat_e6=<int32> lon_e6=<int32>
 *  speed_kmh_x10=<int16> epoch=<uint32>"
 *
 * lat_e6 / lon_e6  : latitude / longitude scaled by 1e6 (microdegrees).
 *                    Valid only when available=1. -90e6..+90e6 /
 * -180e6..+180e6. speed_kmh_x10    : speed-over-ground in 0.1 km/h units. Below
 * the project minimum threshold the firmware clamps to 0. epoch            :
 * UTC Unix epoch derived from the GPS RMC sentence; 0 when GPS time is not yet
 * synced.
 *
 * available=0 implies the lat/lon/speed/epoch fields are stale or
 * unset; hosts must not consume them. The reply is always single-line
 * so the SC frame parser handles it without continuation logic. */
#define SC_REPLY_GPS_FMT                                                       \
  SC_STATUS_OK                                                                 \
  " " SC_REPLY_TAG_GPS                                                         \
  " available=%u lat_e6=%ld lon_e6=%ld speed_kmh_x10=%d epoch=%lu"

/* ── Functional tests ──────────────────────────────────────────────── */

/* "SC_OK TEST_LIST count=<n> names=<name>,<name>,..." - the tests this
 * build offers to the configurator, in registry order. A build without
 * functional tests answers count=0 and an empty names value. */
#define SC_REPLY_TEST_LIST_FMT                                                 \
  SC_STATUS_OK " " SC_REPLY_TAG_TEST_LIST " count=%u names=%s"

/* "SC_OK TEST_INFO name=<test> seq=<0|1> params=<id>,<id>,..." - seq=1 when
 * the test belongs to the sequence started by SC_TEST_RUN all. */
#define SC_REPLY_TEST_INFO_FMT                                                 \
  SC_STATUS_OK " " SC_REPLY_TAG_TEST_INFO " name=%s seq=%u params=%s"

/* "SC_OK TEST_PARAM id=<id> test=<test> value=<n> min=<n> max=<n>
 *  default=<n> unit=<token>" - one runtime parameter of a test. Values are
 * integers in the unit named by the token (count, s, ms, pct_per_s). The
 * value returns to its default after a restart. */
#define SC_REPLY_TEST_PARAM_FMT                                                \
  SC_STATUS_OK " " SC_REPLY_TAG_TEST_PARAM                                     \
               " id=%s test=%s value=%ld min=%ld max=%ld default=%ld unit=%s"

/* "SC_OK TEST_SET id=<id> value=<n>" */
#define SC_REPLY_TEST_SET_FMT                                                  \
  SC_STATUS_OK " " SC_REPLY_TAG_TEST_SET " id=%s value=%ld"

/* "SC_OK TEST_RUN name=<test|all>" - the request is queued for the core
 * that owns the actuator; SC_TEST_STATUS shows when it starts. */
#define SC_REPLY_TEST_RUN_FMT SC_STATUS_OK " " SC_REPLY_TAG_TEST_RUN " name=%s"

#define SC_REPLY_TEST_STOP SC_STATUS_OK " " SC_REPLY_TAG_TEST_STOP
#define SC_REPLY_TEST_SKIP SC_STATUS_OK " " SC_REPLY_TAG_TEST_SKIP

/* "SC_OK TEST_STATUS state=<idle|running> ..." followed by key=value
 * tokens: test, src (sc|console), seq (<i>/<n>, only in a sequence),
 * elapsed_ms, demand_x10 and position_x10 (percent of the usable stroke
 * times ten), the running test's own progress keys, and last/result for
 * the most recent finished test. Unknown keys are ignored by the host. */
#define SC_REPLY_TEST_STATUS_HEAD SC_STATUS_OK " " SC_REPLY_TAG_TEST_STATUS

/* "SC_BAD_REQUEST unknown_test name=<test>" */
#define SC_REPLY_TEST_UNKNOWN_FMT                                              \
  SC_STATUS_BAD_REQUEST " " SC_REPLY_REASON_UNKNOWN_TEST " name=%s"

/* "SC_NOT_READY ENGINE_RUNNING" - the engine-speed interlock refused a
 * test start. */
#define SC_REPLY_NOT_READY_ENGINE_RUNNING                                      \
  SC_STATUS_NOT_READY " " SC_REPLY_TAG_ENGINE_RUNNING

/* "SC_NOT_READY BUSY" - an earlier test request is still queued. */
#define SC_REPLY_NOT_READY_BUSY SC_STATUS_NOT_READY " " SC_REPLY_TAG_BUSY

#ifdef __cplusplus
}
#endif
