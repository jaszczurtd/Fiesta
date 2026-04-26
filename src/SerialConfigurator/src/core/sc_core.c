#include "sc_core.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ScModuleDef {
    const char *token;
    const char *display_name;
} ScModuleDef;

static const ScModuleDef k_module_defs[SC_MODULE_COUNT] = {
    { "ECU", "ECU" },
    { "Clocks", "Clocks" },
    { "OIL&SPD", "OilAndSpeed" },
};

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == 0 || dst_size == 0u) {
        return;
    }

    if (src == 0) {
        dst[0] = '\0';
        return;
    }

    (void)snprintf(dst, dst_size, "%s", src);
}

static void copy_span(
    char *dst,
    size_t dst_size,
    const char *start,
    const char *end
)
{
    if (dst == 0 || dst_size == 0u) {
        return;
    }

    if (start == 0 || end == 0 || end <= start) {
        dst[0] = '\0';
        return;
    }

    size_t len = (size_t)(end - start);
    if (len >= dst_size) {
        len = dst_size - 1u;
    }

    (void)memcpy(dst, start, len);
    dst[len] = '\0';
}

static void log_append(char *log_output, size_t log_output_size, const char *format, ...)
{
    if (log_output == 0 || log_output_size == 0u || format == 0) {
        return;
    }

    const size_t current_len = strlen(log_output);
    if (current_len >= log_output_size - 1u) {
        return;
    }

    va_list args;
    va_start(args, format);
    (void)vsnprintf(
        log_output + current_len,
        log_output_size - current_len,
        format,
        args
    );
    va_end(args);
}

static void identity_reset(ScIdentityData *identity)
{
    if (identity == 0) {
        return;
    }

    identity->valid = false;
    identity->module_name[0] = '\0';
    identity->proto_version = 0;
    identity->proto_present = false;
    identity->session_id = 0u;
    identity->session_present = false;
    identity->fw_version[0] = '\0';
    identity->build_id[0] = '\0';
    identity->uid[0] = '\0';
}

static bool parse_int_strict(const char *text, int *value)
{
    if (text == 0 || value == 0 || text[0] == '\0') {
        return false;
    }

    errno = 0;
    char *end = 0;
    const long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }

    *value = (int)parsed;
    return true;
}

static bool parse_u32_strict(const char *text, uint32_t *value)
{
    if (text == 0 || value == 0 || text[0] == '\0') {
        return false;
    }

    errno = 0;
    char *end = 0;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static void identity_set_field(ScIdentityData *identity, const char *key, const char *value)
{
    if (identity == 0 || key == 0 || value == 0) {
        return;
    }

    if (strcmp(key, "module") == 0) {
        copy_string(identity->module_name, sizeof(identity->module_name), value);
        return;
    }

    if (strcmp(key, "proto") == 0) {
        int proto = 0;
        if (parse_int_strict(value, &proto)) {
            identity->proto_version = proto;
            identity->proto_present = true;
        }
        return;
    }

    if (strcmp(key, "session") == 0) {
        uint32_t session_id = 0u;
        if (parse_u32_strict(value, &session_id)) {
            identity->session_id = session_id;
            identity->session_present = true;
        }
        return;
    }

    if (strcmp(key, "fw") == 0) {
        copy_string(identity->fw_version, sizeof(identity->fw_version), value);
        return;
    }

    if (strcmp(key, "build") == 0) {
        copy_string(identity->build_id, sizeof(identity->build_id), value);
        return;
    }

    if (strcmp(key, "uid") == 0) {
        copy_string(identity->uid, sizeof(identity->uid), value);
    }
}

static void parse_identity_fields(const char *response, ScIdentityData *identity)
{
    if (identity == 0) {
        return;
    }

    identity_reset(identity);
    if (response == 0 || response[0] == '\0') {
        return;
    }

    const char *cursor = response;
    while (cursor[0] != '\0') {
        while (cursor[0] == ' ') {
            cursor++;
        }

        if (cursor[0] == '\0') {
            break;
        }

        const char *token_start = cursor;
        while (cursor[0] != '\0' && cursor[0] != ' ') {
            cursor++;
        }

        const size_t token_len = (size_t)(cursor - token_start);
        if (token_len == 0u) {
            continue;
        }

        const char *eq = (const char *)memchr(token_start, '=', token_len);
        if (eq == 0 || eq == token_start || eq == token_start + token_len - 1u) {
            continue;
        }

        char key[24];
        char value[SC_IDENTITY_FIELD_MAX];

        copy_span(key, sizeof(key), token_start, eq);
        copy_span(value, sizeof(value), eq + 1, token_start + token_len);
        identity_set_field(identity, key, value);
    }

    identity->valid = identity->module_name[0] != '\0';
}

static bool parse_hello_identity(const char *response, ScIdentityData *identity)
{
    if (response == 0 || identity == 0 || strncmp(response, "OK HELLO", 8) != 0) {
        identity_reset(identity);
        return false;
    }

    parse_identity_fields(response, identity);
    return identity->valid;
}

static bool parse_meta_identity(const char *response, ScIdentityData *identity)
{
    if (response == 0 || identity == 0 || strncmp(response, "SC_OK META", 10) != 0) {
        identity_reset(identity);
        return false;
    }

    parse_identity_fields(response, identity);
    return identity->valid;
}

static int module_index_from_token(const char *token)
{
    if (token == 0) {
        return -1;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        if (strcmp(token, k_module_defs[i].token) == 0) {
            return (int)i;
        }
    }

    if (strcmp(token, "OilAndSpeed") == 0) {
        return 2;
    }

    return -1;
}

static bool all_modules_detected(const ScCore *core)
{
    if (core == 0) {
        return false;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        if (!core->modules[i].detected) {
            return false;
        }
    }

    return true;
}

static void command_result_reset(ScCommandResult *result)
{
    if (result == 0) {
        return;
    }

    result->status = SC_COMMAND_STATUS_UNPARSEABLE;
    result->status_token[0] = '\0';
    result->topic[0] = '\0';
    result->details[0] = '\0';
    result->response[0] = '\0';
}

static const char *skip_spaces(const char *cursor)
{
    if (cursor == 0) {
        return 0;
    }

    while (cursor[0] == ' ') {
        cursor++;
    }

    return cursor;
}

static const char *token_end(const char *cursor)
{
    if (cursor == 0) {
        return 0;
    }

    while (cursor[0] != '\0' && cursor[0] != ' ') {
        cursor++;
    }

    return cursor;
}

static ScCommandStatus command_status_from_token(const char *token)
{
    if (token == 0 || token[0] == '\0') {
        return SC_COMMAND_STATUS_UNPARSEABLE;
    }

    if (strcmp(token, "SC_OK") == 0) {
        return SC_COMMAND_STATUS_OK;
    }
    if (strcmp(token, "SC_UNKNOWN_CMD") == 0) {
        return SC_COMMAND_STATUS_UNKNOWN_CMD;
    }
    if (strcmp(token, "SC_BAD_REQUEST") == 0) {
        return SC_COMMAND_STATUS_BAD_REQUEST;
    }
    if (strcmp(token, "SC_NOT_READY") == 0) {
        return SC_COMMAND_STATUS_NOT_READY;
    }
    if (strcmp(token, "SC_INVALID_PARAM_ID") == 0) {
        return SC_COMMAND_STATUS_INVALID_PARAM_ID;
    }

    if (strncmp(token, "SC_", 3) == 0) {
        return SC_COMMAND_STATUS_OTHER;
    }

    return SC_COMMAND_STATUS_UNPARSEABLE;
}

static void parse_sc_command_result(const char *response, ScCommandResult *result)
{
    if (result == 0) {
        return;
    }

    command_result_reset(result);
    copy_string(result->response, sizeof(result->response), response);

    if (response == 0 || response[0] == '\0') {
        return;
    }

    const char *cursor = skip_spaces(response);
    if (cursor == 0 || cursor[0] == '\0') {
        return;
    }

    const char *status_start = cursor;
    const char *status_end = token_end(status_start);
    copy_span(result->status_token, sizeof(result->status_token), status_start, status_end);
    result->status = command_status_from_token(result->status_token);

    cursor = skip_spaces(status_end);
    if (cursor == 0 || cursor[0] == '\0') {
        return;
    }

    const char *topic_start = cursor;
    const char *topic_end_ptr = token_end(topic_start);
    const bool has_equals = memchr(topic_start, '=', (size_t)(topic_end_ptr - topic_start)) != 0;
    if (!has_equals) {
        copy_span(result->topic, sizeof(result->topic), topic_start, topic_end_ptr);
        cursor = skip_spaces(topic_end_ptr);
    }

    if (strcmp(result->status_token, "ERR") == 0 && strcmp(result->topic, "UNKNOWN") == 0) {
        result->status = SC_COMMAND_STATUS_UNKNOWN_CMD;
        copy_string(result->status_token, sizeof(result->status_token), "SC_UNKNOWN_CMD");
        result->topic[0] = '\0';
    }

    copy_string(result->details, sizeof(result->details), cursor != 0 ? cursor : "");
}

static bool sc_core_send_sc_command_internal(
    ScCore *core,
    size_t module_index,
    const char *command,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
)
{
    if (result != 0) {
        command_result_reset(result);
    }

    if (core == 0) {
        log_append(log_output, log_output_size, "[ERROR] Core is not initialized.\n");
        return false;
    }

    if (result == 0) {
        log_append(log_output, log_output_size, "[ERROR] Command result buffer is NULL.\n");
        return false;
    }

    if (command == 0 || command[0] == '\0') {
        log_append(log_output, log_output_size, "[ERROR] SC command is empty.\n");
        return false;
    }

    if (module_index >= SC_MODULE_COUNT) {
        log_append(log_output, log_output_size, "[ERROR] Module index out of range: %zu\n", module_index);
        return false;
    }

    ScModuleStatus *status = &core->modules[module_index];
    if (!status->detected || status->port_path[0] == '\0') {
        log_append(
            log_output,
            log_output_size,
            "[ERROR] Module '%s' is not currently detected.\n",
            status->display_name
        );
        return false;
    }

    if (status->target_ambiguous) {
        log_append(
            log_output,
            log_output_size,
            "[ERROR] Refusing command '%s' for '%s': ambiguous target (%zu instances).\n",
            command,
            status->display_name,
            status->detected_instances
        );
        return false;
    }

    char response[SC_HELLO_RESPONSE_MAX];
    char error[256];
    response[0] = '\0';
    error[0] = '\0';

    if (!sc_transport_send_sc_command(
            &core->transport,
            status->port_path,
            command,
            response,
            sizeof(response),
            error,
            sizeof(error)
        )) {
        log_append(
            log_output,
            log_output_size,
            "[ERROR] %s on %s failed: %s\n",
            command,
            status->port_path,
            error
        );
        return false;
    }

    parse_sc_command_result(response, result);
    log_append(
        log_output,
        log_output_size,
        "%s on %s -> %s\n",
        command,
        status->port_path,
        result->response
    );
    return true;
}

void sc_core_init(ScCore *core)
{
    if (core == 0) {
        return;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        core->modules[i].display_name = k_module_defs[i].display_name;
    }

    sc_transport_init_default(&core->transport);
    sc_core_reset_detection(core);
}

void sc_core_set_transport(ScCore *core, const ScTransport *transport)
{
    if (core == 0) {
        return;
    }

    if (transport == 0) {
        sc_transport_init_default(&core->transport);
        return;
    }

    core->transport = *transport;
}

void sc_core_reset_detection(ScCore *core)
{
    if (core == 0) {
        return;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        core->modules[i].detected = false;
        core->modules[i].detected_instances = 0u;
        core->modules[i].target_ambiguous = false;
        core->modules[i].port_path[0] = '\0';
        core->modules[i].hello_response[0] = '\0';
        identity_reset(&core->modules[i].hello_identity);
        identity_reset(&core->modules[i].meta_identity);
    }
}

void sc_core_detect_modules(ScCore *core, char *log_output, size_t log_output_size)
{
    if (log_output != 0 && log_output_size > 0u) {
        log_output[0] = '\0';
    }

    if (core == 0) {
        log_append(log_output, log_output_size, "[ERROR] Core is not initialized.\n");
        return;
    }

    sc_core_reset_detection(core);
    log_append(log_output, log_output_size, "Detecting Fiesta modules with HELLO...\n");

    ScTransportCandidateList candidates;
    char list_error[256];
    list_error[0] = '\0';

    if (!sc_transport_list_candidates(&core->transport, &candidates, list_error, sizeof(list_error))) {
        log_append(
            log_output,
            log_output_size,
            "[ERROR] Candidate enumeration failed: %s\n",
            list_error
        );
        return;
    }

    if (candidates.count == 0u) {
        log_append(
            log_output,
            log_output_size,
            "No Fiesta serial devices found in /dev/serial/by-id.\n"
        );
        return;
    }

    log_append(log_output, log_output_size, "Candidates: %zu\n", candidates.count);
    if (candidates.truncated) {
        log_append(
            log_output,
            log_output_size,
            "[WARN] Candidate list was truncated at %u entries.\n",
            SC_TRANSPORT_MAX_CANDIDATES
        );
    }

    for (size_t i = 0u; i < candidates.count; ++i) {
        const char *candidate_path = candidates.paths[i];
        char device_path[SC_PORT_PATH_MAX];
        char resolve_error[256];
        device_path[0] = '\0';
        resolve_error[0] = '\0';

        if (!sc_transport_resolve_device_path(
                &core->transport,
                candidate_path,
                device_path,
                sizeof(device_path),
                resolve_error,
                sizeof(resolve_error)
            )) {
            log_append(
                log_output,
                log_output_size,
                "\n[%zu/%zu] %s -> resolve failed: %s\n",
                i + 1u,
                candidates.count,
                candidate_path,
                resolve_error
            );
            continue;
        }

        log_append(
            log_output,
            log_output_size,
            "\n[%zu/%zu] %s -> %s\n",
            i + 1u,
            candidates.count,
            candidate_path,
            device_path
        );

        char response[SC_HELLO_RESPONSE_MAX];
        char error[256];
        response[0] = '\0';
        error[0] = '\0';

        if (!sc_transport_send_hello(
                &core->transport,
                device_path,
                response,
                sizeof(response),
                error,
                sizeof(error)
            )) {
            log_append(log_output, log_output_size, "HELLO failed: %s\n", error);
            continue;
        }

        log_append(log_output, log_output_size, "HELLO response: %s\n", response);

        if (strncmp(response, "OK HELLO", 8) != 0) {
            log_append(log_output, log_output_size, "Ignored: response is not OK HELLO.\n");
            continue;
        }

        ScIdentityData parsed_identity;
        if (!parse_hello_identity(response, &parsed_identity)) {
            log_append(
                log_output,
                log_output_size,
                "Ignored: HELLO does not include a valid module identity.\n"
            );
            continue;
        }

        const int module_index = module_index_from_token(parsed_identity.module_name);
        if (module_index < 0) {
            log_append(
                log_output,
                log_output_size,
                "Ignored: unsupported module token '%s'.\n",
                parsed_identity.module_name
            );
            continue;
        }

        ScModuleStatus *status = &core->modules[module_index];
        status->detected_instances++;
        if (status->detected) {
            status->target_ambiguous = true;
            log_append(
                log_output,
                log_output_size,
                "Module '%s' appears multiple times (%zu instances). Marked ambiguous.\n",
                status->display_name,
                status->detected_instances
            );
        }

        status->detected = true;
        copy_string(status->port_path, sizeof(status->port_path), device_path);
        copy_string(status->hello_response, sizeof(status->hello_response), response);
        status->hello_identity = parsed_identity;

        log_append(
            log_output,
            log_output_size,
            "Detected module '%s' on %s\n",
            status->display_name,
            device_path
        );

        if (all_modules_detected(core)) {
            log_append(
                log_output,
                log_output_size,
                "All known modules are detected. Stopping scan early.\n"
            );
            break;
        }
    }

    log_append(log_output, log_output_size, "\nDetection summary:\n");
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        const ScModuleStatus *status = &core->modules[i];
        log_append(
            log_output,
            log_output_size,
            "- %-12s : %s (instances=%zu%s)\n",
            status->display_name,
            status->detected ? "DETECTED" : "NOT DETECTED",
            status->detected_instances,
            status->target_ambiguous ? ", ambiguous" : ""
        );
    }
}

size_t sc_core_module_count(void)
{
    return SC_MODULE_COUNT;
}

const ScModuleStatus *sc_core_module_status(const ScCore *core, size_t index)
{
    if (core == 0 || index >= SC_MODULE_COUNT) {
        return 0;
    }

    return &core->modules[index];
}

const char *sc_command_status_name(ScCommandStatus status)
{
    switch (status) {
        case SC_COMMAND_STATUS_UNPARSEABLE:
            return "UNPARSEABLE";
        case SC_COMMAND_STATUS_OK:
            return "SC_OK";
        case SC_COMMAND_STATUS_UNKNOWN_CMD:
            return "SC_UNKNOWN_CMD";
        case SC_COMMAND_STATUS_BAD_REQUEST:
            return "SC_BAD_REQUEST";
        case SC_COMMAND_STATUS_NOT_READY:
            return "SC_NOT_READY";
        case SC_COMMAND_STATUS_INVALID_PARAM_ID:
            return "SC_INVALID_PARAM_ID";
        case SC_COMMAND_STATUS_OTHER:
            return "SC_OTHER";
        default:
            return "UNKNOWN";
    }
}

bool sc_core_sc_get_meta(
    ScCore *core,
    size_t module_index,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
)
{
    const bool success = sc_core_send_sc_command_internal(
        core,
        module_index,
        "SC_GET_META",
        result,
        log_output,
        log_output_size
    );
    if (!success || core == 0 || result == 0 || module_index >= SC_MODULE_COUNT) {
        return success;
    }

    if (result->status == SC_COMMAND_STATUS_OK && strcmp(result->topic, "META") == 0) {
        ScIdentityData parsed;
        if (parse_meta_identity(result->response, &parsed)) {
            core->modules[module_index].meta_identity = parsed;
        } else {
            log_append(
                log_output,
                log_output_size,
                "[WARN] SC_GET_META returned SC_OK META but identity fields were incomplete.\n"
            );
        }
    }

    return true;
}

bool sc_core_sc_get_param_list(
    ScCore *core,
    size_t module_index,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
)
{
    return sc_core_send_sc_command_internal(
        core,
        module_index,
        "SC_GET_PARAM_LIST",
        result,
        log_output,
        log_output_size
    );
}

bool sc_core_sc_get_values(
    ScCore *core,
    size_t module_index,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
)
{
    return sc_core_send_sc_command_internal(
        core,
        module_index,
        "SC_GET_VALUES",
        result,
        log_output,
        log_output_size
    );
}

bool sc_core_sc_get_param(
    ScCore *core,
    size_t module_index,
    const char *param_id,
    ScCommandResult *result,
    char *log_output,
    size_t log_output_size
)
{
    if (param_id == 0 || param_id[0] == '\0') {
        if (result != 0) {
            command_result_reset(result);
        }
        log_append(log_output, log_output_size, "[ERROR] param_id is required for SC_GET_PARAM.\n");
        return false;
    }

    for (size_t i = 0u; param_id[i] != '\0'; ++i) {
        if (isspace((unsigned char)param_id[i])) {
            if (result != 0) {
                command_result_reset(result);
            }
            log_append(
                log_output,
                log_output_size,
                "[ERROR] param_id must not contain whitespace: '%s'\n",
                param_id
            );
            return false;
        }
    }

    char command[SC_HELLO_RESPONSE_MAX];
    const int written = snprintf(command, sizeof(command), "SC_GET_PARAM %s", param_id);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        if (result != 0) {
            command_result_reset(result);
        }
        log_append(log_output, log_output_size, "[ERROR] SC_GET_PARAM command is too long.\n");
        return false;
    }

    return sc_core_send_sc_command_internal(
        core,
        module_index,
        command,
        result,
        log_output,
        log_output_size
    );
}
