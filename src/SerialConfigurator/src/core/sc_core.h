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

#ifdef __cplusplus
}
#endif

#endif /* SC_CORE_H */
