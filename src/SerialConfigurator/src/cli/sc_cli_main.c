#include "sc_core.h"
#include "sc_manifest.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define CLI_DETECTION_LOG_MAX 16384u
#define CLI_COMMAND_LOG_MAX 4096u

typedef struct CliSelectors {
    const char *module;
    const char *uid;
    const char *port;
} CliSelectors;

static const char *value_or_dash(const char *value)
{
    return (value != 0 && value[0] != '\0') ? value : "-";
}

static void typed_value_to_text(const ScTypedValue *value, char *buffer, size_t buffer_size)
{
    if (buffer == 0 || buffer_size == 0u) {
        return;
    }

    if (value == 0) {
        (void)snprintf(buffer, buffer_size, "-");
        return;
    }

    switch (value->type) {
        case SC_VALUE_TYPE_BOOL:
            (void)snprintf(buffer, buffer_size, "%s", value->bool_value ? "true" : "false");
            return;
        case SC_VALUE_TYPE_INT:
            (void)snprintf(buffer, buffer_size, "%lld", (long long)value->int_value);
            return;
        case SC_VALUE_TYPE_UINT:
            (void)snprintf(buffer, buffer_size, "%llu", (unsigned long long)value->uint_value);
            return;
        case SC_VALUE_TYPE_FLOAT:
            (void)snprintf(buffer, buffer_size, "%.6g", value->float_value);
            return;
        case SC_VALUE_TYPE_TEXT:
        case SC_VALUE_TYPE_UNKNOWN:
        default:
            (void)snprintf(buffer, buffer_size, "%s", value_or_dash(value->raw));
            return;
    }
}

static bool strings_equal_case_insensitive(const char *a, const char *b)
{
    if (a == 0 || b == 0) {
        return false;
    }

    size_t i = 0u;
    while (a[i] != '\0' && b[i] != '\0') {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
        i++;
    }

    return a[i] == '\0' && b[i] == '\0';
}

static void print_usage(const char *program_name)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s detect\n", program_name);
    fprintf(stderr, "  %s list\n", program_name);
    fprintf(stderr, "  %s meta [--module <name>] [--uid <hex>] [--port <path>]\n", program_name);
    fprintf(stderr, "  %s param-list [--module <name>] [--uid <hex>] [--port <path>]\n", program_name);
    fprintf(stderr, "  %s get-values [--module <name>] [--uid <hex>] [--port <path>]\n", program_name);
    fprintf(stderr, "  %s get-param <param-id> [--module <name>] [--uid <hex>] [--port <path>]\n", program_name);
    fprintf(stderr, "  %s reboot-bootloader [--module <name>] [--uid <hex>] [--port <path>]\n",
            program_name);
    fprintf(stderr, "      [--manifest <path>] [--artifact <path>]\n");
}

static bool parse_selectors(
    int argc,
    char *argv[],
    int start_index,
    CliSelectors *selectors
)
{
    if (selectors == 0) {
        return false;
    }

    selectors->module = 0;
    selectors->uid = 0;
    selectors->port = 0;

    int i = start_index;
    while (i < argc) {
        const char *arg = argv[i];
        if (strcmp(arg, "--module") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] Missing value for --module\n");
                return false;
            }
            selectors->module = argv[i + 1];
            i += 2;
            continue;
        }

        if (strcmp(arg, "--uid") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] Missing value for --uid\n");
                return false;
            }
            selectors->uid = argv[i + 1];
            i += 2;
            continue;
        }

        if (strcmp(arg, "--port") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] Missing value for --port\n");
                return false;
            }
            selectors->port = argv[i + 1];
            i += 2;
            continue;
        }

        fprintf(stderr, "[ERROR] Unknown option: %s\n", arg);
        return false;
    }

    return true;
}

static void print_module_table(const ScCore *core)
{
    if (core == 0) {
        return;
    }

    printf(
        "%-12s %-8s %-9s %-10s %-22s %-14s %-10s %-10s\n",
        "MODULE",
        "FOUND",
        "INSTANCES",
        "AMBIGUOUS",
        "PORT",
        "UID",
        "FW",
        "BUILD"
    );

    for (size_t i = 0u; i < sc_core_module_count(); ++i) {
        const ScModuleStatus *status = sc_core_module_status(core, i);
        if (status == 0) {
            continue;
        }

        printf(
            "%-12s %-8s %-9zu %-10s %-22s %-14s %-10s %-10s\n",
            status->display_name,
            status->detected ? "yes" : "no",
            status->detected_instances,
            status->target_ambiguous ? "yes" : "no",
            status->detected ? status->port_path : "-",
            status->detected ? value_or_dash(status->hello_identity.uid) : "-",
            status->detected ? value_or_dash(status->hello_identity.fw_version) : "-",
            status->detected ? value_or_dash(status->hello_identity.build_id) : "-"
        );
    }
}

static bool module_matches_selectors(const ScModuleStatus *status, const CliSelectors *selectors)
{
    if (status == 0 || selectors == 0 || !status->detected) {
        return false;
    }

    if (selectors->module != 0) {
        const bool module_match =
            strings_equal_case_insensitive(status->display_name, selectors->module) ||
            strings_equal_case_insensitive(status->hello_identity.module_name, selectors->module);
        if (!module_match) {
            return false;
        }
    }

    if (selectors->uid != 0) {
        if (!strings_equal_case_insensitive(status->hello_identity.uid, selectors->uid)) {
            return false;
        }
    }

    if (selectors->port != 0) {
        if (strcmp(status->port_path, selectors->port) != 0) {
            return false;
        }
    }

    return true;
}

static int select_target_module(
    const ScCore *core,
    const CliSelectors *selectors,
    char *error,
    size_t error_size
)
{
    if (error != 0 && error_size > 0u) {
        error[0] = '\0';
    }

    if (core == 0 || selectors == 0) {
        if (error != 0 && error_size > 0u) {
            (void)snprintf(error, error_size, "internal error: missing core/selectors");
        }
        return -1;
    }

    const bool has_selector =
        selectors->module != 0 || selectors->uid != 0 || selectors->port != 0;

    size_t detected_count = 0u;
    int only_detected_index = -1;
    size_t matched_count = 0u;
    int matched_index = -1;

    for (size_t i = 0u; i < sc_core_module_count(); ++i) {
        const ScModuleStatus *status = sc_core_module_status(core, i);
        if (status == 0 || !status->detected) {
            continue;
        }

        detected_count++;
        only_detected_index = (int)i;

        if (has_selector && !module_matches_selectors(status, selectors)) {
            continue;
        }

        matched_count++;
        matched_index = (int)i;
    }

    if (!has_selector) {
        if (detected_count == 0u) {
            (void)snprintf(error, error_size, "No detected modules to target.");
            return -1;
        }

        if (detected_count != 1u) {
            (void)snprintf(
                error,
                error_size,
                "Ambiguous target: %zu modules detected. Provide --module, --uid, or --port.",
                detected_count
            );
            return -1;
        }

        matched_index = only_detected_index;
    } else {
        if (matched_count == 0u) {
            (void)snprintf(error, error_size, "No module matches provided selectors.");
            return -1;
        }

        if (matched_count > 1u) {
            (void)snprintf(
                error,
                error_size,
                "Ambiguous selectors: %zu modules match. Refusing to continue.",
                matched_count
            );
            return -1;
        }
    }

    if (matched_index < 0) {
        (void)snprintf(error, error_size, "Unable to resolve target module.");
        return -1;
    }

    const ScModuleStatus *target = sc_core_module_status(core, (size_t)matched_index);
    if (target == 0) {
        (void)snprintf(error, error_size, "Resolved target index is invalid.");
        return -1;
    }

    if (target->target_ambiguous) {
        (void)snprintf(
            error,
            error_size,
            "Fail-closed: target '%s' appears on multiple devices (%zu instances).",
            target->display_name,
            target->detected_instances
        );
        return -1;
    }

    return matched_index;
}

static bool run_detection(ScCore *core, char *log, size_t log_size)
{
    if (core == 0 || log == 0 || log_size == 0u) {
        return false;
    }

    sc_core_init(core);
    sc_core_detect_modules(core, log, log_size);
    return true;
}

static int command_detect(void)
{
    ScCore core;
    char detection_log[CLI_DETECTION_LOG_MAX];

    if (!run_detection(&core, detection_log, sizeof(detection_log))) {
        fprintf(stderr, "[ERROR] Detection initialization failed.\n");
        return 2;
    }

    printf("%s", detection_log);
    return 0;
}

static int command_list(void)
{
    ScCore core;
    char detection_log[CLI_DETECTION_LOG_MAX];

    if (!run_detection(&core, detection_log, sizeof(detection_log))) {
        fprintf(stderr, "[ERROR] Detection initialization failed.\n");
        return 2;
    }

    print_module_table(&core);
    return 0;
}

static void print_parsed_values(const ScParamValuesData *values)
{
    if (values == 0) {
        return;
    }

    printf("PARSED count=%zu%s\n", values->count, values->truncated ? " (truncated)" : "");
    for (size_t i = 0u; i < values->count; ++i) {
        char typed[128];
        typed_value_to_text(&values->entries[i].value, typed, sizeof(typed));
        printf(
            "- %s = %s (type=%s)\n",
            values->entries[i].id,
            typed,
            sc_value_type_name(values->entries[i].value.type)
        );
    }
}

static void print_parsed_param_list(const ScParamListData *list)
{
    if (list == 0) {
        return;
    }

    printf("PARSED count=%zu%s\n", list->count, list->truncated ? " (truncated)" : "");
    for (size_t i = 0u; i < list->count; ++i) {
        printf("- %s\n", list->ids[i]);
    }
}

static void print_parsed_param_detail(const ScParamDetailData *detail)
{
    if (detail == 0) {
        return;
    }

    char value_text[128];
    char min_text[128];
    char max_text[128];
    char default_text[128];
    typed_value_to_text(&detail->value, value_text, sizeof(value_text));
    typed_value_to_text(&detail->min, min_text, sizeof(min_text));
    typed_value_to_text(&detail->max, max_text, sizeof(max_text));
    typed_value_to_text(&detail->default_value, default_text, sizeof(default_text));

    printf("PARSED id=%s valid=%s\n", detail->id, detail->valid ? "true" : "false");
    printf("  value=%s (type=%s)\n", value_text, sc_value_type_name(detail->value.type));
    if (detail->has_min) {
        printf("  min=%s (type=%s)\n", min_text, sc_value_type_name(detail->min.type));
    }
    if (detail->has_max) {
        printf("  max=%s (type=%s)\n", max_text, sc_value_type_name(detail->max.type));
    }
    if (detail->has_default) {
        printf(
            "  default=%s (type=%s)\n",
            default_text,
            sc_value_type_name(detail->default_value.type)
        );
    }
}

static bool parse_get_param_args(
    int argc,
    char *argv[],
    const char **param_id,
    CliSelectors *selectors
)
{
    if (param_id == 0 || selectors == 0) {
        return false;
    }

    *param_id = 0;
    selectors->module = 0;
    selectors->uid = 0;
    selectors->port = 0;

    int i = 2;
    while (i < argc) {
        const char *arg = argv[i];
        if (strcmp(arg, "--module") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] Missing value for --module\n");
                return false;
            }
            selectors->module = argv[i + 1];
            i += 2;
            continue;
        }

        if (strcmp(arg, "--uid") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] Missing value for --uid\n");
                return false;
            }
            selectors->uid = argv[i + 1];
            i += 2;
            continue;
        }

        if (strcmp(arg, "--port") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] Missing value for --port\n");
                return false;
            }
            selectors->port = argv[i + 1];
            i += 2;
            continue;
        }

        if (*param_id == 0) {
            *param_id = arg;
            i++;
            continue;
        }

        fprintf(stderr, "[ERROR] Unknown option or extra argument: %s\n", arg);
        return false;
    }

    if (*param_id == 0 || (*param_id)[0] == '\0') {
        fprintf(stderr, "[ERROR] Missing <param-id>.\n");
        return false;
    }

    return true;
}

static int command_meta_values_or_catalog(
    const char *command,
    const char *param_id,
    const CliSelectors *selectors
)
{
    ScCore core;
    char detection_log[CLI_DETECTION_LOG_MAX];
    char command_log[CLI_COMMAND_LOG_MAX];
    command_log[0] = '\0';

    if (!run_detection(&core, detection_log, sizeof(detection_log))) {
        fprintf(stderr, "[ERROR] Detection initialization failed.\n");
        return 2;
    }

    char selection_error[256];
    const int module_index = select_target_module(
        &core,
        selectors,
        selection_error,
        sizeof(selection_error)
    );
    if (module_index < 0) {
        fprintf(stderr, "[ERROR] %s\n", selection_error);
        print_module_table(&core);
        return 3;
    }

    ScCommandResult result;
    bool ok = false;
    if (strcmp(command, "meta") == 0) {
        ok = sc_core_sc_get_meta(
            &core,
            (size_t)module_index,
            &result,
            command_log,
            sizeof(command_log)
        );
    } else if (strcmp(command, "get-values") == 0) {
        ok = sc_core_sc_get_values(
            &core,
            (size_t)module_index,
            &result,
            command_log,
            sizeof(command_log)
        );
    } else if (strcmp(command, "param-list") == 0) {
        ok = sc_core_sc_get_param_list(
            &core,
            (size_t)module_index,
            &result,
            command_log,
            sizeof(command_log)
        );
    } else if (strcmp(command, "get-param") == 0) {
        ok = sc_core_sc_get_param(
            &core,
            (size_t)module_index,
            param_id,
            &result,
            command_log,
            sizeof(command_log)
        );
    }

    if (!ok) {
        fprintf(stderr, "[ERROR] Command transport failed.\n");
        fprintf(stderr, "%s", command_log);
        return 4;
    }

    printf("%s\n", result.response);

    if (strcmp(command, "meta") == 0 && result.status == SC_COMMAND_STATUS_OK) {
        const ScModuleStatus *status = sc_core_module_status(&core, (size_t)module_index);
        if (status != 0 && status->meta_identity.valid) {
            printf(
                "PARSED module=%s proto=%s session=%s fw=%s build=%s uid=%s\n",
                value_or_dash(status->meta_identity.module_name),
                status->meta_identity.proto_present ? "set" : "-",
                status->meta_identity.session_present ? "set" : "-",
                value_or_dash(status->meta_identity.fw_version),
                value_or_dash(status->meta_identity.build_id),
                value_or_dash(status->meta_identity.uid)
            );
        }
    }

    if (strcmp(command, "param-list") == 0 && result.status == SC_COMMAND_STATUS_OK) {
        ScParamListData parsed;
        char parse_error[256];
        if (sc_core_parse_param_list_result(&result, &parsed, parse_error, sizeof(parse_error))) {
            print_parsed_param_list(&parsed);
        } else {
            fprintf(stderr, "[WARN] %s\n", parse_error);
        }
    }

    if (strcmp(command, "get-values") == 0 && result.status == SC_COMMAND_STATUS_OK) {
        ScParamValuesData parsed;
        char parse_error[256];
        if (sc_core_parse_param_values_result(&result, &parsed, parse_error, sizeof(parse_error))) {
            print_parsed_values(&parsed);
        } else {
            fprintf(stderr, "[WARN] %s\n", parse_error);
        }
    }

    if (strcmp(command, "get-param") == 0 && result.status == SC_COMMAND_STATUS_OK) {
        ScParamDetailData parsed;
        char parse_error[256];
        if (sc_core_parse_param_result(&result, &parsed, parse_error, sizeof(parse_error))) {
            print_parsed_param_detail(&parsed);
        } else {
            fprintf(stderr, "[WARN] %s\n", parse_error);
        }
    }

    if (result.status != SC_COMMAND_STATUS_OK) {
        fprintf(
            stderr,
            "[ERROR] Device returned non-OK status: %s\n",
            sc_command_status_name(result.status)
        );
        return 5;
    }

    return 0;
}

/* ── reboot-bootloader ─────────────────────────────────────────────────── */

static bool parse_reboot_args(int argc, char *argv[], CliSelectors *selectors,
                              const char **manifest_path,
                              const char **artifact_path)
{
    *manifest_path = NULL;
    *artifact_path = NULL;
    selectors->module = NULL;
    selectors->uid = NULL;
    selectors->port = NULL;
    for (int i = 2; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--module") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] Missing value for --module\n");
                return false;
            }
            selectors->module = argv[++i];
        } else if (strcmp(arg, "--uid") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] Missing value for --uid\n");
                return false;
            }
            selectors->uid = argv[++i];
        } else if (strcmp(arg, "--port") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] Missing value for --port\n");
                return false;
            }
            selectors->port = argv[++i];
        } else if (strcmp(arg, "--manifest") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] Missing value for --manifest\n");
                return false;
            }
            *manifest_path = argv[++i];
        } else if (strcmp(arg, "--artifact") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] Missing value for --artifact\n");
                return false;
            }
            *artifact_path = argv[++i];
        } else {
            fprintf(stderr, "[ERROR] Unknown option: %s\n", arg);
            return false;
        }
    }
    return true;
}

static int command_reboot_bootloader(int argc, char *argv[])
{
    CliSelectors selectors;
    const char *manifest_path = NULL;
    const char *artifact_path = NULL;
    if (!parse_reboot_args(argc, argv, &selectors,
                           &manifest_path, &artifact_path)) {
        return 1;
    }

    /* Phase 4 preflight (optional). The manifest verifies the artifact
     * SHA-256 and module-name match before we hand the firmware over to
     * the boot ROM. Hard-reject on any mismatch — the doc requires the
     * flashing flow to fail closed. */
    sc_manifest_t manifest;
    bool have_manifest = false;
    if (manifest_path != NULL) {
        const sc_manifest_status_t st = sc_manifest_load_file(manifest_path,
                                                              &manifest);
        if (st != SC_MANIFEST_OK) {
            fprintf(stderr, "[ERROR] manifest load: %s\n",
                    sc_manifest_status_str(st));
            return 3;
        }
        have_manifest = true;

        if (artifact_path != NULL) {
            const sc_manifest_status_t av =
                sc_manifest_verify_artifact(&manifest, artifact_path);
            if (av != SC_MANIFEST_OK) {
                fprintf(stderr, "[ERROR] artifact verify: %s\n",
                        sc_manifest_status_str(av));
                return 3;
            }
            printf("[OK] manifest sha256 matches artifact %s\n", artifact_path);
        } else {
            fprintf(stderr,
                    "[WARN] --manifest provided without --artifact; "
                    "manifest fields parsed but SHA-256 not verified.\n");
        }
    }

    ScCore core;
    char detection_log[CLI_DETECTION_LOG_MAX];
    if (!run_detection(&core, detection_log, sizeof(detection_log))) {
        fprintf(stderr, "[ERROR] Detection initialization failed.\n");
        return 2;
    }

    char selection_error[256];
    selection_error[0] = '\0';
    const int idx = select_target_module(&core, &selectors,
                                         selection_error,
                                         sizeof(selection_error));
    if (idx < 0) {
        fprintf(stderr, "[ERROR] %s\n", selection_error);
        return 4;
    }

    const ScModuleStatus *target = sc_core_module_status(&core, (size_t)idx);
    if (target == NULL || !target->detected) {
        fprintf(stderr, "[ERROR] Selected module is not detected.\n");
        return 4;
    }

    if (have_manifest) {
        const sc_manifest_status_t mm =
            sc_manifest_check_module_match(&manifest, target->display_name);
        if (mm != SC_MANIFEST_OK) {
            fprintf(stderr,
                    "[ERROR] manifest module mismatch: manifest=%s target=%s\n",
                    manifest.module_name, target->display_name);
            return 3;
        }
        printf("[OK] manifest module=%s matches target.\n",
               target->display_name);
    }

    char err[512];
    err[0] = '\0';
    const ScAuthStatus auth_st = sc_core_authenticate(&core.transport,
                                                      target->port_path,
                                                      err, sizeof(err));
    if (auth_st != SC_AUTH_OK) {
        fprintf(stderr, "[ERROR] auth: %s — %s\n",
                sc_auth_status_name(auth_st), err);
        return 5;
    }
    printf("[OK] authenticated session on %s\n", target->port_path);

    err[0] = '\0';
    const ScRebootStatus reboot_st = sc_core_reboot_to_bootloader(
        &core.transport, target->port_path, err, sizeof(err));
    if (reboot_st != SC_REBOOT_OK) {
        fprintf(stderr, "[ERROR] reboot: %s — %s\n",
                sc_reboot_status_name(reboot_st), err);
        return 6;
    }

    printf("[OK] firmware acknowledged SC_REBOOT_BOOTLOADER on %s.\n",
           target->port_path);
    printf("Port should disappear shortly; pick up the BOOTSEL/UF2 device "
           "from there (Phase 6).\n");
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *command = argv[1];

    if (strcmp(command, "detect") == 0) {
        return command_detect();
    }

    if (strcmp(command, "reboot-bootloader") == 0) {
        return command_reboot_bootloader(argc, argv);
    }

    if (strcmp(command, "list") == 0) {
        return command_list();
    }

    if (strcmp(command, "meta") == 0 ||
        strcmp(command, "get-values") == 0 ||
        strcmp(command, "param-list") == 0) {
        CliSelectors selectors;
        if (!parse_selectors(argc, argv, 2, &selectors)) {
            print_usage(argv[0]);
            return 1;
        }

        return command_meta_values_or_catalog(
            command,
            0,
            &selectors
        );
    }

    if (strcmp(command, "get-param") == 0) {
        CliSelectors selectors;
        const char *param_id = 0;
        if (!parse_get_param_args(argc, argv, &param_id, &selectors)) {
            print_usage(argv[0]);
            return 1;
        }

        return command_meta_values_or_catalog(
            "get-param",
            param_id,
            &selectors
        );
    }

    fprintf(stderr, "[ERROR] Unknown command: %s\n", command);
    print_usage(argv[0]);
    return 1;
}
