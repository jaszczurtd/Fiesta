#include "sc_core.h"

#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "[FAIL] %s\n", msg); \
    return 1; \
} } while (0)

typedef struct MockDevice {
    const char *candidate_path;
    const char *device_path;
    const char *hello_response;
    const char *meta_response;
    const char *values_response;
    const char *param_list_response;
    const char *param_nominal_response;
    const char *param_unknown_response;
} MockDevice;

typedef struct MockTransportContext {
    const MockDevice *devices;
    size_t device_count;
} MockTransportContext;

static const MockDevice *find_device_by_candidate(
    const MockTransportContext *context,
    const char *candidate_path
)
{
    if (context == 0 || candidate_path == 0) {
        return 0;
    }

    for (size_t i = 0u; i < context->device_count; ++i) {
        if (strcmp(context->devices[i].candidate_path, candidate_path) == 0) {
            return &context->devices[i];
        }
    }

    return 0;
}

static const MockDevice *find_device_by_path(
    const MockTransportContext *context,
    const char *device_path
)
{
    if (context == 0 || device_path == 0) {
        return 0;
    }

    for (size_t i = 0u; i < context->device_count; ++i) {
        if (strcmp(context->devices[i].device_path, device_path) == 0) {
            return &context->devices[i];
        }
    }

    return 0;
}

static bool mock_list_candidates(
    void *context_ptr,
    ScTransportCandidateList *list,
    char *error,
    size_t error_size
)
{
    (void)error;
    (void)error_size;

    MockTransportContext *context = (MockTransportContext *)context_ptr;
    if (context == 0 || list == 0) {
        return false;
    }

    list->count = 0u;
    list->truncated = false;

    for (size_t i = 0u; i < context->device_count; ++i) {
        if (list->count >= SC_TRANSPORT_MAX_CANDIDATES) {
            list->truncated = true;
            break;
        }

        (void)snprintf(
            list->paths[list->count],
            sizeof(list->paths[list->count]),
            "%s",
            context->devices[i].candidate_path
        );
        list->count++;
    }

    return true;
}

static bool mock_resolve_device_path(
    void *context_ptr,
    const char *candidate_path,
    char *device_path,
    size_t device_path_size,
    char *error,
    size_t error_size
)
{
    MockTransportContext *context = (MockTransportContext *)context_ptr;
    const MockDevice *device = find_device_by_candidate(context, candidate_path);
    if (device == 0 || device_path == 0 || device_path_size == 0u) {
        (void)snprintf(error, error_size, "mock resolve failed");
        return false;
    }

    (void)snprintf(device_path, device_path_size, "%s", device->device_path);
    return true;
}

static bool mock_send_hello(
    void *context_ptr,
    const char *device_path,
    char *response,
    size_t response_size,
    char *error,
    size_t error_size
)
{
    MockTransportContext *context = (MockTransportContext *)context_ptr;
    const MockDevice *device = find_device_by_path(context, device_path);
    if (device == 0 || response == 0 || response_size == 0u) {
        (void)snprintf(error, error_size, "mock hello failed");
        return false;
    }

    (void)snprintf(response, response_size, "%s", device->hello_response);
    return true;
}

static bool mock_send_sc_command(
    void *context_ptr,
    const char *device_path,
    const char *command,
    char *response,
    size_t response_size,
    char *error,
    size_t error_size
)
{
    MockTransportContext *context = (MockTransportContext *)context_ptr;
    const MockDevice *device = find_device_by_path(context, device_path);
    if (device == 0 || command == 0 || response == 0 || response_size == 0u) {
        (void)snprintf(error, error_size, "mock SC command failed");
        return false;
    }

    if (strcmp(command, "SC_GET_META") == 0) {
        (void)snprintf(response, response_size, "%s", device->meta_response);
        return true;
    }

    if (strcmp(command, "SC_GET_VALUES") == 0) {
        (void)snprintf(response, response_size, "%s", device->values_response);
        return true;
    }

    if (strcmp(command, "SC_GET_PARAM_LIST") == 0) {
        (void)snprintf(response, response_size, "%s", device->param_list_response);
        return true;
    }

    if (strcmp(command, "SC_GET_PARAM nominal_rpm") == 0) {
        (void)snprintf(response, response_size, "%s", device->param_nominal_response);
        return true;
    }

    if (strncmp(command, "SC_GET_PARAM ", strlen("SC_GET_PARAM ")) == 0) {
        (void)snprintf(response, response_size, "%s", device->param_unknown_response);
        return true;
    }

    (void)snprintf(response, response_size, "SC_UNKNOWN_CMD");
    return true;
}

static ScTransport make_mock_transport(MockTransportContext *context)
{
    static const ScTransportOps ops = {
        .list_candidates = mock_list_candidates,
        .resolve_device_path = mock_resolve_device_path,
        .send_hello = mock_send_hello,
        .send_sc_command = mock_send_sc_command,
    };

    ScTransport transport;
    sc_transport_init_custom(&transport, &ops, context);
    return transport;
}

static int test_detect_parses_structured_hello_fields(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/ecu",
            .device_path = "/dev/mock/ttyACM0",
            .hello_response = "OK HELLO module=ECU proto=1 session=42 fw=v1 build=b42 uid=E661A4",
            .meta_response = "SC_OK META module=ECU proto=1 session=42 fw=v1 build=b42 uid=E661A4",
            .values_response = "SC_OK PARAM_VALUES nominal_rpm=890",
            .param_list_response = "SC_OK PARAM_LIST nominal_rpm",
            .param_nominal_response = "SC_OK PARAM id=nominal_rpm value=890 min=700 max=1200 default=890",
            .param_unknown_response = "SC_INVALID_PARAM_ID id=unknown",
        }
    };
    MockTransportContext context = {
        .devices = devices,
        .device_count = sizeof(devices) / sizeof(devices[0]),
    };

    ScCore core;
    sc_core_init(&core);
    const ScTransport transport = make_mock_transport(&context);
    sc_core_set_transport(&core, &transport);

    char log[2048];
    sc_core_detect_modules(&core, log, sizeof(log));

    const ScModuleStatus *ecu = sc_core_module_status(&core, 0u);
    TEST_ASSERT(ecu != 0, "ECU status missing");
    TEST_ASSERT(ecu->detected, "ECU should be detected");
    TEST_ASSERT(ecu->detected_instances == 1u, "ECU instances should be 1");
    TEST_ASSERT(!ecu->target_ambiguous, "ECU should not be ambiguous");
    TEST_ASSERT(strcmp(ecu->port_path, "/dev/mock/ttyACM0") == 0, "ECU path mismatch");
    TEST_ASSERT(ecu->hello_identity.valid, "hello identity should be valid");
    TEST_ASSERT(strcmp(ecu->hello_identity.module_name, "ECU") == 0, "hello module mismatch");
    TEST_ASSERT(ecu->hello_identity.proto_present, "hello proto should be present");
    TEST_ASSERT(ecu->hello_identity.proto_version == 1, "hello proto mismatch");
    TEST_ASSERT(ecu->hello_identity.session_present, "hello session should be present");
    TEST_ASSERT(ecu->hello_identity.session_id == 42u, "hello session mismatch");
    TEST_ASSERT(strcmp(ecu->hello_identity.fw_version, "v1") == 0, "hello fw mismatch");
    TEST_ASSERT(strcmp(ecu->hello_identity.build_id, "b42") == 0, "hello build mismatch");
    TEST_ASSERT(strcmp(ecu->hello_identity.uid, "E661A4") == 0, "hello uid mismatch");
    return 0;
}

static int test_duplicate_detection_sets_ambiguous_flag(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/ecu-1",
            .device_path = "/dev/mock/ttyACM0",
            .hello_response = "OK HELLO module=ECU proto=1 session=11 fw=v1 build=b1 uid=AAA",
            .meta_response = "SC_OK META module=ECU proto=1 session=11 fw=v1 build=b1 uid=AAA",
            .values_response = "SC_OK PARAM_VALUES nominal_rpm=890",
            .param_list_response = "SC_OK PARAM_LIST nominal_rpm",
            .param_nominal_response = "SC_OK PARAM id=nominal_rpm value=890 min=700 max=1200 default=890",
            .param_unknown_response = "SC_INVALID_PARAM_ID id=unknown",
        },
        {
            .candidate_path = "/dev/mock/by-id/ecu-2",
            .device_path = "/dev/mock/ttyACM1",
            .hello_response = "OK HELLO module=ECU proto=1 session=12 fw=v1 build=b1 uid=BBB",
            .meta_response = "SC_OK META module=ECU proto=1 session=12 fw=v1 build=b1 uid=BBB",
            .values_response = "SC_OK PARAM_VALUES nominal_rpm=890",
            .param_list_response = "SC_OK PARAM_LIST nominal_rpm",
            .param_nominal_response = "SC_OK PARAM id=nominal_rpm value=890 min=700 max=1200 default=890",
            .param_unknown_response = "SC_INVALID_PARAM_ID id=unknown",
        }
    };
    MockTransportContext context = {
        .devices = devices,
        .device_count = sizeof(devices) / sizeof(devices[0]),
    };

    ScCore core;
    sc_core_init(&core);
    const ScTransport transport = make_mock_transport(&context);
    sc_core_set_transport(&core, &transport);

    char log[2048];
    sc_core_detect_modules(&core, log, sizeof(log));

    const ScModuleStatus *ecu = sc_core_module_status(&core, 0u);
    TEST_ASSERT(ecu != 0, "ECU status missing");
    TEST_ASSERT(ecu->detected, "ECU should be detected");
    TEST_ASSERT(ecu->detected_instances == 2u, "ECU instances should be 2");
    TEST_ASSERT(ecu->target_ambiguous, "ECU should be marked ambiguous");
    return 0;
}

static int test_sc_commands_parse_status_and_meta(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/ecu",
            .device_path = "/dev/mock/ttyACM0",
            .hello_response = "OK HELLO module=ECU proto=1 session=42 fw=v1 build=b42 uid=E661A4",
            .meta_response = "SC_OK META module=ECU proto=1 session=99 fw=v2 build=b99 uid=F00D",
            .values_response = "SC_OK PARAM_VALUES nominal_rpm=900",
            .param_list_response = "SC_OK PARAM_LIST nominal_rpm",
            .param_nominal_response = "SC_OK PARAM id=nominal_rpm value=900 min=700 max=1200 default=890",
            .param_unknown_response = "SC_INVALID_PARAM_ID id=unknown",
        }
    };
    MockTransportContext context = {
        .devices = devices,
        .device_count = sizeof(devices) / sizeof(devices[0]),
    };

    ScCore core;
    sc_core_init(&core);
    const ScTransport transport = make_mock_transport(&context);
    sc_core_set_transport(&core, &transport);

    char log[2048];
    sc_core_detect_modules(&core, log, sizeof(log));

    ScCommandResult result;
    char cmd_log[1024];

    TEST_ASSERT(sc_core_sc_get_meta(&core, 0u, &result, cmd_log, sizeof(cmd_log)), "SC_GET_META call failed");
    TEST_ASSERT(result.status == SC_COMMAND_STATUS_OK, "SC_GET_META status should be OK");
    TEST_ASSERT(strcmp(result.status_token, "SC_OK") == 0, "SC_GET_META status token mismatch");
    TEST_ASSERT(strcmp(result.topic, "META") == 0, "SC_GET_META topic mismatch");

    const ScModuleStatus *ecu = sc_core_module_status(&core, 0u);
    TEST_ASSERT(ecu != 0, "ECU status missing after SC_GET_META");
    TEST_ASSERT(ecu->meta_identity.valid, "meta identity should be parsed");
    TEST_ASSERT(strcmp(ecu->meta_identity.module_name, "ECU") == 0, "meta module mismatch");
    TEST_ASSERT(ecu->meta_identity.session_present, "meta session should be present");
    TEST_ASSERT(ecu->meta_identity.session_id == 99u, "meta session mismatch");
    TEST_ASSERT(strcmp(ecu->meta_identity.uid, "F00D") == 0, "meta uid mismatch");

    TEST_ASSERT(sc_core_sc_get_values(&core, 0u, &result, cmd_log, sizeof(cmd_log)), "SC_GET_VALUES call failed");
    TEST_ASSERT(result.status == SC_COMMAND_STATUS_OK, "SC_GET_VALUES status should be OK");
    TEST_ASSERT(strcmp(result.topic, "PARAM_VALUES") == 0, "SC_GET_VALUES topic mismatch");
    return 0;
}

static int test_sc_get_param_reports_invalid_param_id(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/ecu",
            .device_path = "/dev/mock/ttyACM0",
            .hello_response = "OK HELLO module=ECU proto=1 session=42 fw=v1 build=b42 uid=E661A4",
            .meta_response = "SC_OK META module=ECU proto=1 session=42 fw=v1 build=b42 uid=E661A4",
            .values_response = "SC_OK PARAM_VALUES nominal_rpm=890",
            .param_list_response = "SC_OK PARAM_LIST nominal_rpm",
            .param_nominal_response = "SC_OK PARAM id=nominal_rpm value=890 min=700 max=1200 default=890",
            .param_unknown_response = "SC_INVALID_PARAM_ID id=unknown_param",
        }
    };
    MockTransportContext context = {
        .devices = devices,
        .device_count = sizeof(devices) / sizeof(devices[0]),
    };

    ScCore core;
    sc_core_init(&core);
    const ScTransport transport = make_mock_transport(&context);
    sc_core_set_transport(&core, &transport);

    char detect_log[2048];
    char command_log[1024];
    sc_core_detect_modules(&core, detect_log, sizeof(detect_log));

    ScCommandResult result;
    TEST_ASSERT(
        sc_core_sc_get_param(&core, 0u, "unknown_param", &result, command_log, sizeof(command_log)),
        "SC_GET_PARAM call failed"
    );
    TEST_ASSERT(
        result.status == SC_COMMAND_STATUS_INVALID_PARAM_ID,
        "SC_GET_PARAM should return SC_INVALID_PARAM_ID"
    );
    TEST_ASSERT(
        strcmp(result.status_token, "SC_INVALID_PARAM_ID") == 0,
        "SC_GET_PARAM status token mismatch"
    );
    return 0;
}

static int test_sc_err_unknown_is_mapped_to_unknown_cmd_status(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/clocks",
            .device_path = "/dev/mock/ttyACM7",
            .hello_response = "OK HELLO module=Clocks proto=1 session=10 fw=v1 build=b1 uid=CLOCKS",
            .meta_response = "ERR UNKNOWN",
            .values_response = "ERR UNKNOWN",
            .param_list_response = "ERR UNKNOWN",
            .param_nominal_response = "ERR UNKNOWN",
            .param_unknown_response = "ERR UNKNOWN",
        }
    };
    MockTransportContext context = {
        .devices = devices,
        .device_count = sizeof(devices) / sizeof(devices[0]),
    };

    ScCore core;
    sc_core_init(&core);
    const ScTransport transport = make_mock_transport(&context);
    sc_core_set_transport(&core, &transport);

    char detect_log[2048];
    char command_log[1024];
    sc_core_detect_modules(&core, detect_log, sizeof(detect_log));

    ScCommandResult result;
    TEST_ASSERT(
        sc_core_sc_get_meta(&core, 1u, &result, command_log, sizeof(command_log)),
        "SC_GET_META call failed for ERR UNKNOWN mapping test"
    );
    TEST_ASSERT(
        result.status == SC_COMMAND_STATUS_UNKNOWN_CMD,
        "ERR UNKNOWN should map to SC_COMMAND_STATUS_UNKNOWN_CMD"
    );
    TEST_ASSERT(
        strcmp(result.status_token, "SC_UNKNOWN_CMD") == 0,
        "ERR UNKNOWN status token should be normalized"
    );
    return 0;
}

int main(void)
{
    if (test_detect_parses_structured_hello_fields() != 0) {
        return 1;
    }

    if (test_duplicate_detection_sets_ambiguous_flag() != 0) {
        return 1;
    }

    if (test_sc_commands_parse_status_and_meta() != 0) {
        return 1;
    }

    if (test_sc_get_param_reports_invalid_param_id() != 0) {
        return 1;
    }

    if (test_sc_err_unknown_is_mapped_to_unknown_cmd_status() != 0) {
        return 1;
    }

    printf("[OK] serial_configurator_core protocol read-only tests passed\n");
    return 0;
}
