#ifndef SC_CORE_H
#define SC_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sc_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SC_MODULE_COUNT 3u
#define SC_PORT_PATH_MAX SC_TRANSPORT_PATH_MAX
#define SC_HELLO_RESPONSE_MAX SC_TRANSPORT_RESPONSE_MAX
#define SC_IDENTITY_FIELD_MAX 64u
#define SC_COMMAND_STATUS_TOKEN_MAX 32u
#define SC_COMMAND_TOPIC_MAX 32u
#define SC_PARAM_ID_MAX 48u
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

#ifdef __cplusplus
}
#endif

#endif /* SC_CORE_H */
