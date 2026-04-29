#ifndef SC_CORE_H
#define SC_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sc_protocol.h"
#include "sc_transport.h"
#include "../config.h"
#include "../../common/scDefinitions/sc_fiesta_module_tokens.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SC_PORT_PATH_MAX SC_TRANSPORT_PATH_MAX
#define SC_HELLO_RESPONSE_MAX SC_TRANSPORT_RESPONSE_MAX
#define SC_IDENTITY_FIELD_MAX 64u
#define SC_COMMAND_STATUS_TOKEN_MAX 32u
#define SC_COMMAND_TOPIC_MAX 32u
/* SC_PARAM_ID_MAX is defined in sc_protocol.h (single source of truth). */
#define SC_PARAM_TEXT_MAX 96u
#define SC_PARAM_ITEMS_MAX 64u

typedef struct ScIdentityData {
    bool valid;
    char module_name[SC_IDENTITY_FIELD_MAX];
    int proto_version;
    bool proto_present;
    uint32_t session_id;
    bool session_present;
    char fw_version[SC_IDENTITY_FIELD_MAX];
    char build_id[SC_IDENTITY_FIELD_MAX];
    char uid[SC_IDENTITY_FIELD_MAX];
} ScIdentityData;

typedef enum ScCommandStatus {
    SC_COMMAND_STATUS_UNPARSEABLE = 0,
    SC_COMMAND_STATUS_OK,
    SC_COMMAND_STATUS_UNKNOWN_CMD,
    SC_COMMAND_STATUS_BAD_REQUEST,
    SC_COMMAND_STATUS_NOT_READY,
    SC_COMMAND_STATUS_INVALID_PARAM_ID,
    SC_COMMAND_STATUS_OTHER
} ScCommandStatus;

typedef struct ScCommandResult {
    ScCommandStatus status;
    char status_token[SC_COMMAND_STATUS_TOKEN_MAX];
    char topic[SC_COMMAND_TOPIC_MAX];
    char details[SC_HELLO_RESPONSE_MAX];
    char response[SC_HELLO_RESPONSE_MAX];
} ScCommandResult;

typedef enum ScValueType {
    SC_VALUE_TYPE_UNKNOWN = 0,
    SC_VALUE_TYPE_BOOL,
    SC_VALUE_TYPE_INT,
    SC_VALUE_TYPE_UINT,
    SC_VALUE_TYPE_FLOAT,
    SC_VALUE_TYPE_TEXT
} ScValueType;

typedef struct ScTypedValue {
    ScValueType type;
    bool bool_value;
    int64_t int_value;
    uint64_t uint_value;
    double float_value;
    char raw[SC_PARAM_TEXT_MAX];
} ScTypedValue;

typedef struct ScParamValueEntry {
    char id[SC_PARAM_ID_MAX];
    ScTypedValue value;
} ScParamValueEntry;

typedef struct ScParamListData {
    size_t count;
    bool truncated;
    char ids[SC_PARAM_ITEMS_MAX][SC_PARAM_ID_MAX];
} ScParamListData;

typedef struct ScParamValuesData {
    size_t count;
    bool truncated;
    ScParamValueEntry entries[SC_PARAM_ITEMS_MAX];
} ScParamValuesData;

typedef struct ScParamDetailData {
    bool valid;
    char id[SC_PARAM_ID_MAX];
    bool has_value;
    ScTypedValue value;
    bool has_min;
    ScTypedValue min;
    bool has_max;
    ScTypedValue max;
    bool has_default;
    ScTypedValue default_value;
} ScParamDetailData;

typedef struct ScModuleStatus {
    const char *display_name;
    bool detected;
    size_t detected_instances;
    bool target_ambiguous;
    char port_path[SC_PORT_PATH_MAX];
    char hello_response[SC_HELLO_RESPONSE_MAX];
    ScIdentityData hello_identity;
    ScIdentityData meta_identity;
} ScModuleStatus;

typedef struct ScCore {
    ScModuleStatus modules[SC_MODULE_COUNT];
    ScTransport transport;
} ScCore;

void sc_core_init(ScCore *core);
void sc_core_set_transport(ScCore *core, const ScTransport *transport);
void sc_core_reset_detection(ScCore *core);
void sc_core_detect_modules(ScCore *core, char *log_output, size_t log_output_size);
size_t sc_core_module_count(void);
const ScModuleStatus *sc_core_module_status(const ScCore *core, size_t index);
const char *sc_command_status_name(ScCommandStatus status);
const char *sc_value_type_name(ScValueType type);

bool sc_core_sc_get_meta(
    ScCore *core,
    size_t module_index,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
);
bool sc_core_sc_get_param_list(
    ScCore *core,
    size_t module_index,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
);
bool sc_core_sc_get_values(
    ScCore *core,
    size_t module_index,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
);
bool sc_core_sc_get_param(
    ScCore *core,
    size_t module_index,
    const char *param_id,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
);

bool sc_core_parse_param_list_result(
    const ScCommandResult *result,
    ScParamListData *parsed,
    char *error,
    size_t error_size
);
bool sc_core_parse_param_values_result(
    const ScCommandResult *result,
    ScParamValuesData *parsed,
    char *error,
    size_t error_size
);
bool sc_core_parse_param_result(
    const ScCommandResult *result,
    ScParamDetailData *parsed,
    char *error,
    size_t error_size
);

/* ── Phase 5: authenticated bootloader entry ────────────────────────── */

typedef enum ScAuthStatus {
    SC_AUTH_OK = 0,
    SC_AUTH_ERR_NULL_ARG,
    SC_AUTH_ERR_HELLO_FAILED,
    SC_AUTH_ERR_HELLO_PARSE,
    SC_AUTH_ERR_BEGIN_FAILED,
    SC_AUTH_ERR_BAD_CHALLENGE,
    SC_AUTH_ERR_RESPONSE_COMPUTE,
    SC_AUTH_ERR_PROVE_FAILED,
    SC_AUTH_ERR_AUTH_REJECTED
} ScAuthStatus;

typedef enum ScRebootStatus {
    SC_REBOOT_OK = 0,
    SC_REBOOT_ERR_NULL_ARG,
    SC_REBOOT_ERR_TRANSPORT,
    SC_REBOOT_ERR_NOT_AUTHORIZED,
    SC_REBOOT_ERR_UNEXPECTED_REPLY
} ScRebootStatus;

const char *sc_auth_status_name(ScAuthStatus status);
const char *sc_reboot_status_name(ScRebootStatus status);

/**
 * @brief Run HELLO -> SC_AUTH_BEGIN -> SC_AUTH_PROVE on @p device_path.
 *
 * Builds the AUTH_PROVE response from the device's reported UID and the
 * compile-time salt (`sc_auth_compute_response_hex`). On success the
 * firmware-side session is authenticated; the caller can then issue the
 * one auth-gated command available today, `sc_core_reboot_to_bootloader`.
 *
 * @param transport    Transport to use (must not be NULL).
 * @param device_path  Resolved serial device path.
 * @param error        Optional error buffer (filled on non-OK status).
 * @param error_size   Size of @p error.
 * @return @c SC_AUTH_OK on success, otherwise a precise reason code.
 */
ScAuthStatus sc_core_authenticate(
    const ScTransport *transport,
    const char *device_path,
    char *error,
    size_t error_size);

/**
 * @brief Send `SC_REBOOT_BOOTLOADER` and verify the firmware ACK.
 *
 * Requires the firmware-side session to already be authenticated (see
 * @ref sc_core_authenticate). The transport-level cache reuses the same
 * physical USB CDC fd across calls, so the firmware authenticated state
 * persists between auth and this call.
 *
 * On success the firmware will (a) drain the ACK frame and (b) hand
 * control to the boot ROM; the host should stop using the port and let
 * the Phase 6 watcher pick up the BOOTSEL/UF2 mass-storage device.
 */
ScRebootStatus sc_core_reboot_to_bootloader(
    const ScTransport *transport,
    const char *device_path,
    char *error,
    size_t error_size);

/* ── Phase 8.5: parameter staging host orchestrator ───────────────── */

/**
 * @brief Stable status enum for @ref sc_core_set_param.
 *
 * `INVALID_ID`, `READ_ONLY`, `OUT_OF_RANGE` and `NOT_AUTHORIZED` mirror
 * the firmware's reply taxonomy from sc_protocol.h
 * (SC_REPLY_BAD_REQUEST_READ_ONLY_FMT / _OUT_OF_RANGE_FMT,
 *  SC_REPLY_INVALID_PARAM_ID_FMT, SC_STATUS_NOT_AUTHORIZED).
 */
typedef enum ScSetParamStatus {
    SC_SET_PARAM_OK = 0,
    SC_SET_PARAM_ERR_NULL_ARG,
    SC_SET_PARAM_ERR_TRANSPORT,
    SC_SET_PARAM_ERR_NOT_AUTHORIZED,
    SC_SET_PARAM_ERR_INVALID_ID,
    SC_SET_PARAM_ERR_READ_ONLY,
    SC_SET_PARAM_ERR_OUT_OF_RANGE,
    SC_SET_PARAM_ERR_UNEXPECTED_REPLY
} ScSetParamStatus;

/**
 * @brief Stable status enum for @ref sc_core_commit_params.
 *
 * `COMMIT_FAILED` collapses every cross-field rule violation into one
 * code; the precise reason token (`fan_coolant_hysteresis`,
 * `heater_vs_fan_order`, `persist_failed`, …) is written to the
 * caller-supplied @p error buffer as the firmware's verbatim reply.
 * Adding new rule tokens on the firmware side does not break callers
 * that only care about pass/fail.
 */
typedef enum ScCommitParamsStatus {
    SC_COMMIT_PARAMS_OK = 0,
    SC_COMMIT_PARAMS_ERR_NULL_ARG,
    SC_COMMIT_PARAMS_ERR_TRANSPORT,
    SC_COMMIT_PARAMS_ERR_NOT_AUTHORIZED,
    SC_COMMIT_PARAMS_ERR_COMMIT_FAILED,
    SC_COMMIT_PARAMS_ERR_UNEXPECTED_REPLY
} ScCommitParamsStatus;

typedef enum ScRevertParamsStatus {
    SC_REVERT_PARAMS_OK = 0,
    SC_REVERT_PARAMS_ERR_NULL_ARG,
    SC_REVERT_PARAMS_ERR_TRANSPORT,
    SC_REVERT_PARAMS_ERR_NOT_AUTHORIZED,
    SC_REVERT_PARAMS_ERR_UNEXPECTED_REPLY
} ScRevertParamsStatus;

const char *sc_set_param_status_name(ScSetParamStatus status);
const char *sc_commit_params_status_name(ScCommitParamsStatus status);
const char *sc_revert_params_status_name(ScRevertParamsStatus status);

/**
 * @brief Send `SC_SET_PARAM <param_id> <value>` and parse the reply.
 *
 * Caller is responsible for the auth precondition: run
 * @ref sc_core_authenticate first; the transport-level fd cache and
 * the firmware-side authenticated flag persist across calls until the
 * next HELLO. SET_PARAM mutates the firmware's staging mirror only -
 * the active mirror remains untouched until @ref sc_core_commit_params.
 */
ScSetParamStatus sc_core_set_param(
    const ScTransport *transport,
    const char *device_path,
    const char *param_id,
    int16_t value,
    char *error,
    size_t error_size);

/**
 * @brief Send `SC_COMMIT_PARAMS` and parse the reply.
 *
 * On `COMMIT_FAILED` the firmware reply (e.g. "SC_COMMIT_FAILED
 * reason=fan_coolant_hysteresis") is preserved verbatim in @p error
 * so the caller can render the precise rule that fired.
 */
ScCommitParamsStatus sc_core_commit_params(
    const ScTransport *transport,
    const char *device_path,
    char *error,
    size_t error_size);

/**
 * @brief Send `SC_REVERT_PARAMS` and parse the reply.
 *
 * Always succeeds firmware-side once authenticated - revert is a
 * pure data move (active -> staging). Only fails for transport / auth
 * problems.
 */
ScRevertParamsStatus sc_core_revert_params(
    const ScTransport *transport,
    const char *device_path,
    char *error,
    size_t error_size);

/* ── Phase 6.5: end-to-end flashing orchestrator ──────────────────── */

/**
 * @brief Stable status enum returned by @ref sc_core_flash.
 *
 * The reasons are tagged precisely so the GUI / CLI status field
 * can render "what went wrong, where" without parsing free-form
 * text. Tokens emitted by @ref sc_flash_status_name are stable
 * across versions and matched by tests.
 */
typedef enum ScFlashStatus {
    SC_FLASH_STATUS_OK = 0,
    SC_FLASH_STATUS_NULL_ARG,
    SC_FLASH_STATUS_FORMAT_REJECTED,
    SC_FLASH_STATUS_MANIFEST_PARSE_FAILED,
    SC_FLASH_STATUS_MANIFEST_MODULE_MISMATCH,
    SC_FLASH_STATUS_MANIFEST_ARTIFACT_MISMATCH,
    SC_FLASH_STATUS_AUTH_FAILED,
    SC_FLASH_STATUS_REBOOT_FAILED,
    SC_FLASH_STATUS_BOOTSEL_TIMEOUT,
    SC_FLASH_STATUS_COPY_FAILED,
    SC_FLASH_STATUS_REENUM_TIMEOUT,
    SC_FLASH_STATUS_POST_FLASH_HELLO_FAILED,
    SC_FLASH_STATUS_POST_FLASH_FW_MISMATCH
} ScFlashStatus;

/**
 * @brief Progress phase tag passed to the flash callback.
 *
 * Most phases are quasi-instantaneous from the operator's POV and
 * only fire one callback (with @c bytes_total == 0, which the GUI
 * can render as an indeterminate pulse). The COPY phase fires a
 * callback per chunk with non-zero @c bytes_total so the GUI can
 * switch to a determinate fraction.
 */
typedef enum ScFlashPhase {
    SC_FLASH_PHASE_FORMAT_CHECK = 0,
    SC_FLASH_PHASE_MANIFEST_VERIFY,
    SC_FLASH_PHASE_AUTHENTICATE,
    SC_FLASH_PHASE_REBOOT_TO_BOOTLOADER,
    SC_FLASH_PHASE_WAIT_BOOTSEL,
    SC_FLASH_PHASE_COPY,
    SC_FLASH_PHASE_WAIT_REENUM,
    SC_FLASH_PHASE_POST_FLASH_HELLO
} ScFlashPhase;

typedef void (*sc_core_flash_progress_cb)(ScFlashPhase phase,
                                          uint64_t bytes_written,
                                          uint64_t bytes_total,
                                          void *user);

/**
 * @brief Optional knobs for @ref sc_core_flash; pass NULL to use the
 *        production defaults.
 *
 * Tests use this to point the BOOTSEL watcher and the re-enumeration
 * waiter at @c mkdtemp fixtures and to shrink the timeouts so the
 * suite finishes in well under a second.
 */
typedef struct ScFlashOptions {
    /** Override for `/media/$USER` and `/run/media/$USER`. NULL entries
     *  are skipped; an all-NULL array uses the production defaults. */
    const char *bootsel_parents[2];
    /** Override for `/dev/serial/by-id/`. NULL -> production default. */
    const char *by_id_parent;
    /** 0 -> SC_FLASH_DEFAULT_BOOTSEL_TIMEOUT_MS. */
    uint32_t bootsel_timeout_ms;
    /** 0 -> SC_FLASH_DEFAULT_REENUM_TIMEOUT_MS. */
    uint32_t reenum_timeout_ms;
    /** 0 -> SC_FLASH_DEFAULT_REENUM_GRACE_MS. */
    uint32_t grace_after_reenum_ms;
} ScFlashOptions;

/**
 * @brief End-to-end flashing flow. Composes Phase 6.2/6.3/6.4
 *        helpers plus Phase 3/4/5 auth, manifest, and reboot.
 *
 * Order of operations (each step on success):
 *   1. UF2 format check.
 *   2. If @p manifest_path_or_null is non-NULL: parse manifest,
 *      verify module match against the targeted module index,
 *      verify artifact SHA-256 matches the manifest claim.
 *   3. AUTH (HELLO + AUTH_BEGIN + AUTH_PROVE).
 *   4. REBOOT_BOOTLOADER (sends the framed command, drains the
 *      ACK, hands control to the boot ROM).
 *   5. Watch for the BOOTSEL mass-storage drive.
 *   6. Copy `<drive_path>/firmware.uf2`, invoking the progress
 *      callback per 64 KiB chunk.
 *   7. Wait for the new USB-CDC enumeration on the same UID.
 *   8. Grace sleep so the firmware-side session stack settles.
 *   9. HELLO again to confirm the new firmware is running. If the
 *      manifest declared a `fw_version`, the parsed HELLO identity
 *      must match.
 *
 * On any step's failure the function returns the corresponding
 * @ref ScFlashStatus token; the diagnostic in @p error_buf carries
 * the underlying helper's error string for the operator log.
 *
 * @p uid_hex must be the UID the targeted module reported in its
 * pre-flash HELLO - it gates the re-enumeration step against the
 * same physical device so a different module appearing on the bus
 * concurrently does not get mistakenly matched.
 *
 * @p progress_cb may be NULL.
 */
ScFlashStatus sc_core_flash(
    const ScTransport *transport,
    size_t module_index,
    const char *device_path,
    const char *uid_hex,
    const char *uf2_path,
    const char *manifest_path_or_null,
    const ScFlashOptions *options_or_null,
    sc_core_flash_progress_cb progress_cb, void *progress_user,
    char *error_buf, size_t error_size);

const char *sc_flash_status_name(ScFlashStatus status);
const char *sc_flash_phase_name(ScFlashPhase phase);

#ifdef __cplusplus
}
#endif

#endif /* SC_CORE_H */
