#include "sc_core.h"
#include "sc_fiesta_module_tokens.h"

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

    if (strcmp(command, SC_CMD_BYE) == 0) {
        (void)snprintf(response, response_size, "%s", SC_REPLY_BYE_OK);
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
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
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
    TEST_ASSERT(strcmp(ecu->hello_identity.module_name, SC_MODULE_TOKEN_ECU) == 0, "hello module mismatch");
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
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=11 fw=v1 build=YjE= uid=AAA",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_ECU " proto=1 session=11 fw=v1 build=YjE= uid=AAA",
            .values_response = "SC_OK PARAM_VALUES nominal_rpm=890",
            .param_list_response = "SC_OK PARAM_LIST nominal_rpm",
            .param_nominal_response = "SC_OK PARAM id=nominal_rpm value=890 min=700 max=1200 default=890",
            .param_unknown_response = "SC_INVALID_PARAM_ID id=unknown",
        },
        {
            .candidate_path = "/dev/mock/by-id/ecu-2",
            .device_path = "/dev/mock/ttyACM1",
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=12 fw=v1 build=YjE= uid=BBB",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_ECU " proto=1 session=12 fw=v1 build=YjE= uid=BBB",
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
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_ECU " proto=1 session=99 fw=v2 build=Yjk5 uid=F00D",
            .values_response = "SC_OK PARAM_VALUES nominal_rpm=900",
            .param_list_response = "SC_OK PARAM_LIST nominal_rpm",
            .param_nominal_response = "SC_OK PARAM id=nominal_rpm value=900 min=700 max=1200 default=890 group=idle",
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
    TEST_ASSERT(strcmp(ecu->meta_identity.module_name, SC_MODULE_TOKEN_ECU) == 0, "meta module mismatch");
    TEST_ASSERT(ecu->meta_identity.session_present, "meta session should be present");
    TEST_ASSERT(ecu->meta_identity.session_id == 99u, "meta session mismatch");
    TEST_ASSERT(strcmp(ecu->meta_identity.uid, "F00D") == 0, "meta uid mismatch");

    TEST_ASSERT(sc_core_sc_get_values(&core, 0u, &result, cmd_log, sizeof(cmd_log)), "SC_GET_VALUES call failed");
    TEST_ASSERT(result.status == SC_COMMAND_STATUS_OK, "SC_GET_VALUES status should be OK");
    TEST_ASSERT(strcmp(result.topic, "PARAM_VALUES") == 0, "SC_GET_VALUES topic mismatch");

    ScParamValuesData values;
    char parse_error[256];
    TEST_ASSERT(
        sc_core_parse_param_values_result(&result, &values, parse_error, sizeof(parse_error)),
        "SC_GET_VALUES parsed payload should be valid"
    );
    TEST_ASSERT(values.count == 1u, "parsed values should contain exactly one entry");
    TEST_ASSERT(strcmp(values.entries[0].id, "nominal_rpm") == 0, "parsed value id mismatch");
    TEST_ASSERT(values.entries[0].value.type == SC_VALUE_TYPE_UINT, "nominal_rpm type should be UINT");
    TEST_ASSERT(values.entries[0].value.uint_value == 900u, "nominal_rpm value should be 900");

    TEST_ASSERT(
        sc_core_sc_get_param_list(&core, 0u, &result, cmd_log, sizeof(cmd_log)),
        "SC_GET_PARAM_LIST call failed"
    );
    TEST_ASSERT(result.status == SC_COMMAND_STATUS_OK, "SC_GET_PARAM_LIST status should be OK");
    TEST_ASSERT(strcmp(result.topic, "PARAM_LIST") == 0, "SC_GET_PARAM_LIST topic mismatch");

    ScParamListData list;
    TEST_ASSERT(
        sc_core_parse_param_list_result(&result, &list, parse_error, sizeof(parse_error)),
        "SC_GET_PARAM_LIST parsed payload should be valid"
    );
    TEST_ASSERT(list.count == 1u, "parsed param list should have one id");
    TEST_ASSERT(strcmp(list.ids[0], "nominal_rpm") == 0, "parsed param list id mismatch");

    TEST_ASSERT(
        sc_core_sc_get_param(&core, 0u, "nominal_rpm", &result, cmd_log, sizeof(cmd_log)),
        "SC_GET_PARAM nominal_rpm call failed"
    );
    TEST_ASSERT(result.status == SC_COMMAND_STATUS_OK, "SC_GET_PARAM status should be OK");
    TEST_ASSERT(strcmp(result.topic, "PARAM") == 0, "SC_GET_PARAM topic mismatch");

    ScParamDetailData detail;
    TEST_ASSERT(
        sc_core_parse_param_result(&result, &detail, parse_error, sizeof(parse_error)),
        "SC_GET_PARAM parsed payload should be valid"
    );
    TEST_ASSERT(detail.valid, "parsed detail should be valid");
    TEST_ASSERT(strcmp(detail.id, "nominal_rpm") == 0, "parsed detail id mismatch");
    TEST_ASSERT(detail.has_min, "parsed detail should expose min");
    TEST_ASSERT(detail.has_max, "parsed detail should expose max");
    TEST_ASSERT(detail.has_default, "parsed detail should expose default");
    TEST_ASSERT(detail.value.type == SC_VALUE_TYPE_UINT, "parsed detail value type mismatch");
    TEST_ASSERT(detail.value.uint_value == 900u, "parsed detail value mismatch");
    return 0;
}

static int test_sc_get_param_reports_invalid_param_id(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/ecu",
            .device_path = "/dev/mock/ttyACM0",
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
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

static int test_sc_get_param_parser_rejects_invalid_min_max_range(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/ecu-invalid-range",
            .device_path = "/dev/mock/ttyACM5",
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
            .values_response = "SC_OK PARAM_VALUES nominal_rpm=890",
            .param_list_response = "SC_OK PARAM_LIST nominal_rpm",
            .param_nominal_response = "SC_OK PARAM id=nominal_rpm value=890 min=1200 max=700 default=890",
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
        sc_core_sc_get_param(&core, 0u, "nominal_rpm", &result, command_log, sizeof(command_log)),
        "SC_GET_PARAM call failed for invalid-range parser test"
    );
    TEST_ASSERT(result.status == SC_COMMAND_STATUS_OK, "raw command status should still be SC_OK");

    ScParamDetailData parsed;
    char parse_error[256];
    TEST_ASSERT(
        !sc_core_parse_param_result(&result, &parsed, parse_error, sizeof(parse_error)),
        "parsed detail should reject min > max"
    );
    return 0;
}

static int test_sc_err_unknown_is_mapped_to_unknown_cmd_status(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/clocks",
            .device_path = "/dev/mock/ttyACM7",
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_CLOCKS " proto=1 session=10 fw=v1 build=YjE= uid=CLOCKS",
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

static int test_detect_accepts_plain_build_with_spaces(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/ecu-plain-build",
            .device_path = "/dev/mock/ttyACM9",
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=Apr 26 2026 13:31:00 uid=E661A4",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=Apr 26 2026 13:31:00 uid=E661A4",
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
    TEST_ASSERT(
        strcmp(ecu->hello_identity.build_id, "Apr 26 2026 13:31:00") == 0,
        "hello build should preserve plain text with spaces"
    );
    return 0;
}

static int test_meta_accepts_build_without_equals(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/clocks-build-no-eq",
            .device_path = "/dev/mock/ttyACM8",
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_CLOCKS " proto=1 session=10 fw=v1 build=YjE= uid=CLOCKS",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_CLOCKS " proto=1 session=10 fw=v1 buildYjI= uid=CLOCKS",
            .values_response = "SC_OK PARAM_VALUES",
            .param_list_response = "SC_OK PARAM_LIST",
            .param_nominal_response = "SC_INVALID_PARAM_ID id=nominal_rpm",
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

    char detect_log[2048];
    char command_log[1024];
    sc_core_detect_modules(&core, detect_log, sizeof(detect_log));

    ScCommandResult result;
    TEST_ASSERT(
        sc_core_sc_get_meta(&core, 1u, &result, command_log, sizeof(command_log)),
        "SC_GET_META call failed for build-without-equals case"
    );
    TEST_ASSERT(result.status == SC_COMMAND_STATUS_OK, "SC_GET_META should be OK");

    const ScModuleStatus *clocks = sc_core_module_status(&core, 1u);
    TEST_ASSERT(clocks != 0, "Clocks status missing");
    TEST_ASSERT(clocks->meta_identity.valid, "meta identity should be valid");
    TEST_ASSERT(
        strcmp(clocks->meta_identity.build_id, "b2") == 0,
        "build should be parsed from malformed build token without equals"
    );
    return 0;
}

static int test_meta_base64_build_without_padding_is_decoded(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/clocks-build-missing-padding",
            .device_path = "/dev/mock/ttyACM10",
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_CLOCKS " proto=1 session=10 fw=v1 build=QXByIDI2IDIwMjYgMTM6MTI6MzE= uid=CLOCKS",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_CLOCKS " proto=1 session=11 fw=v1 build=QXByIDI2IDIwMjYgMTM6MTI6MzE uid=CLOCKS",
            .values_response = "SC_OK PARAM_VALUES",
            .param_list_response = "SC_OK PARAM_LIST",
            .param_nominal_response = "SC_INVALID_PARAM_ID id=nominal_rpm",
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

    char detect_log[2048];
    char command_log[1024];
    sc_core_detect_modules(&core, detect_log, sizeof(detect_log));

    ScCommandResult result;
    TEST_ASSERT(
        sc_core_sc_get_meta(&core, 1u, &result, command_log, sizeof(command_log)),
        "SC_GET_META call failed for missing-base64-padding case"
    );
    TEST_ASSERT(result.status == SC_COMMAND_STATUS_OK, "SC_GET_META should be OK");

    const ScModuleStatus *clocks = sc_core_module_status(&core, 1u);
    TEST_ASSERT(clocks != 0, "Clocks status missing");
    TEST_ASSERT(clocks->meta_identity.valid, "meta identity should be valid");
    TEST_ASSERT(
        strcmp(clocks->meta_identity.build_id, "Apr 26 2026 13:12:31") == 0,
        "base64 build without padding should decode to plain date"
    );
    return 0;
}

static int test_meta_corrupted_base64_build_falls_back_to_empty_meta_build(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/ecu-corrupted-build",
            .device_path = "/dev/mock/ttyACM6",
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=QXByIDI2IDIwMjYgMTM6MzE6MDA= uid=E661A4",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=QXyIDI2IDIwMjYgMTM6MzE6MDA= uid=E661A4",
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

    char detect_log[2048];
    char command_log[1024];
    sc_core_detect_modules(&core, detect_log, sizeof(detect_log));

    ScCommandResult result;
    TEST_ASSERT(
        sc_core_sc_get_meta(&core, 0u, &result, command_log, sizeof(command_log)),
        "SC_GET_META call failed for corrupted base64 build case"
    );
    TEST_ASSERT(result.status == SC_COMMAND_STATUS_OK, "SC_GET_META should be OK");

    const ScModuleStatus *ecu = sc_core_module_status(&core, 0u);
    TEST_ASSERT(ecu != 0, "ECU status missing");
    TEST_ASSERT(ecu->hello_identity.valid, "hello identity should be valid");
    TEST_ASSERT(
        strcmp(ecu->hello_identity.build_id, "Apr 26 2026 13:31:00") == 0,
        "hello build should decode to plain date"
    );
    TEST_ASSERT(ecu->meta_identity.valid, "meta identity should be valid");
    TEST_ASSERT(
        ecu->meta_identity.build_id[0] == '\0',
        "corrupted base64 meta build should be treated as empty"
    );
    return 0;
}

static int test_param_list_parser_tolerates_noise_tokens(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/ecu-param-list-noise",
            .device_path = "/dev/mock/ttyACM11",
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
            .values_response = "SC_OK PARAM_VALUES fan_coolant_start_c=102",
            .param_list_response = "SC_OK PARAM_LIST fan_coolant_start_c,fan_coolant_stop_c,ECU:,fan_air_start_c",
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

    char detect_log[2048];
    char command_log[1024];
    sc_core_detect_modules(&core, detect_log, sizeof(detect_log));

    ScCommandResult result;
    TEST_ASSERT(
        sc_core_sc_get_param_list(&core, 0u, &result, command_log, sizeof(command_log)),
        "SC_GET_PARAM_LIST call failed for noisy payload case"
    );
    TEST_ASSERT(result.status == SC_COMMAND_STATUS_OK, "SC_GET_PARAM_LIST should be OK");

    ScParamListData parsed;
    char parse_error[256];
    TEST_ASSERT(
        sc_core_parse_param_list_result(&result, &parsed, parse_error, sizeof(parse_error)),
        "param-list parser should tolerate noisy tokens"
    );
    TEST_ASSERT(parsed.count == 3u, "param-list parser should keep valid ids");
    TEST_ASSERT(parsed.truncated, "param-list parser should mark noisy payload as truncated");
    TEST_ASSERT(strcmp(parsed.ids[0], "fan_coolant_start_c") == 0, "first id mismatch");
    TEST_ASSERT(strcmp(parsed.ids[1], "fan_coolant_stop_c") == 0, "second id mismatch");
    TEST_ASSERT(strcmp(parsed.ids[2], "fan_air_start_c") == 0, "third id mismatch");
    return 0;
}

static int test_param_values_parser_tolerates_noise_tokens(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/ecu-param-values-noise",
            .device_path = "/dev/mock/ttyACM12",
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
            .values_response = "SC_OK PARAM_VALUES fan_coolant_start_c=102 fan_coolant_stop_c=95 ECU: thr:6.0 fan_air_start_c=55 fan_air_stop_c=45",
            .param_list_response = "SC_OK PARAM_LIST fan_coolant_start_c fan_coolant_stop_c fan_air_start_c fan_air_stop_c",
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

    char detect_log[2048];
    char command_log[1024];
    sc_core_detect_modules(&core, detect_log, sizeof(detect_log));

    ScCommandResult result;
    TEST_ASSERT(
        sc_core_sc_get_values(&core, 0u, &result, command_log, sizeof(command_log)),
        "SC_GET_VALUES call failed for noisy payload case"
    );
    TEST_ASSERT(result.status == SC_COMMAND_STATUS_OK, "SC_GET_VALUES should be OK");

    ScParamValuesData parsed;
    char parse_error[256];
    TEST_ASSERT(
        sc_core_parse_param_values_result(&result, &parsed, parse_error, sizeof(parse_error)),
        "param-values parser should tolerate noisy tokens"
    );
    TEST_ASSERT(parsed.count == 4u, "param-values parser should keep valid entries");
    TEST_ASSERT(parsed.truncated, "param-values parser should mark noisy payload as truncated");
    TEST_ASSERT(strcmp(parsed.entries[0].id, "fan_coolant_start_c") == 0, "entry[0] id mismatch");
    TEST_ASSERT(strcmp(parsed.entries[1].id, "fan_coolant_stop_c") == 0, "entry[1] id mismatch");
    TEST_ASSERT(strcmp(parsed.entries[2].id, "fan_air_start_c") == 0, "entry[2] id mismatch");
    TEST_ASSERT(strcmp(parsed.entries[3].id, "fan_air_stop_c") == 0, "entry[3] id mismatch");
    return 0;
}

static int test_sc_bye_command_is_sent_and_parsed(void)
{
    const MockDevice devices[] = {
        {
            .candidate_path = "/dev/mock/by-id/ecu-bye",
            .device_path = "/dev/mock/ttyACM13",
            .hello_response = "OK HELLO module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
            .meta_response = "SC_OK META module=" SC_MODULE_TOKEN_ECU " proto=1 session=42 fw=v1 build=YjQy uid=E661A4",
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

    char detect_log[2048];
    char command_log[1024];
    sc_core_detect_modules(&core, detect_log, sizeof(detect_log));

    ScCommandResult result;
    TEST_ASSERT(
        sc_core_sc_bye(&core, 0u, &result, command_log, sizeof(command_log)),
        "SC_BYE call failed"
    );
    TEST_ASSERT(result.status == SC_COMMAND_STATUS_OK, "SC_BYE should return OK");
    TEST_ASSERT(strcmp(result.status_token, SC_STATUS_OK) == 0, "SC_BYE status token mismatch");
    TEST_ASSERT(strcmp(result.topic, SC_REPLY_TAG_BYE) == 0, "SC_BYE topic mismatch");
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

    if (test_sc_get_param_parser_rejects_invalid_min_max_range() != 0) {
        return 1;
    }

    if (test_sc_err_unknown_is_mapped_to_unknown_cmd_status() != 0) {
        return 1;
    }

    if (test_detect_accepts_plain_build_with_spaces() != 0) {
        return 1;
    }

    if (test_meta_accepts_build_without_equals() != 0) {
        return 1;
    }

    if (test_meta_base64_build_without_padding_is_decoded() != 0) {
        return 1;
    }

    if (test_meta_corrupted_base64_build_falls_back_to_empty_meta_build() != 0) {
        return 1;
    }

    if (test_param_list_parser_tolerates_noise_tokens() != 0) {
        return 1;
    }

    if (test_param_values_parser_tolerates_noise_tokens() != 0) {
        return 1;
    }

    if (test_sc_bye_command_is_sent_and_parsed() != 0) {
        return 1;
    }

    printf("[OK] serial_configurator_core protocol read-only tests passed\n");
    return 0;
}
