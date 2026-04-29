#include "sc_cli_commands.h"

#include <stdio.h>
#include <string.h>

#include "sc_cli_output.h"
#include "sc_core.h"
#include "sc_manifest.h"

static bool run_detection(ScCore *core, char *log, size_t log_size)
{
    if (core == 0 || log == 0 || log_size == 0u) {
        return false;
    }

    sc_core_init(core);
    sc_core_detect_modules(core, log, log_size);
    return true;
}

int sc_cli_command_detect(void)
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

int sc_cli_command_list(void)
{
    ScCore core;
    char detection_log[CLI_DETECTION_LOG_MAX];

    if (!run_detection(&core, detection_log, sizeof(detection_log))) {
        fprintf(stderr, "[ERROR] Detection initialization failed.\n");
        return 2;
    }

    sc_cli_print_module_table(&core);
    return 0;
}

int sc_cli_command_meta_values_or_catalog(const char *command,
                                          const char *param_id,
                                          const CliSelectors *selectors)
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
    const int module_index = sc_cli_select_target_module(
        &core,
        selectors,
        selection_error,
        sizeof(selection_error)
    );
    if (module_index < 0) {
        fprintf(stderr, "[ERROR] %s\n", selection_error);
        sc_cli_print_module_table(&core);
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
                sc_cli_value_or_dash(status->meta_identity.module_name),
                status->meta_identity.proto_present ? "set" : "-",
                status->meta_identity.session_present ? "set" : "-",
                sc_cli_value_or_dash(status->meta_identity.fw_version),
                sc_cli_value_or_dash(status->meta_identity.build_id),
                sc_cli_value_or_dash(status->meta_identity.uid)
            );
        }
    }

    if (strcmp(command, "param-list") == 0 && result.status == SC_COMMAND_STATUS_OK) {
        ScParamListData parsed;
        char parse_error[256];
        if (sc_core_parse_param_list_result(&result, &parsed, parse_error, sizeof(parse_error))) {
            sc_cli_print_parsed_param_list(&parsed);
        } else {
            fprintf(stderr, "[WARN] %s\n", parse_error);
        }
    }

    if (strcmp(command, "get-values") == 0 && result.status == SC_COMMAND_STATUS_OK) {
        ScParamValuesData parsed;
        char parse_error[256];
        if (sc_core_parse_param_values_result(&result, &parsed, parse_error, sizeof(parse_error))) {
            sc_cli_print_parsed_values(&parsed);
        } else {
            fprintf(stderr, "[WARN] %s\n", parse_error);
        }
    }

    if (strcmp(command, "get-param") == 0 && result.status == SC_COMMAND_STATUS_OK) {
        ScParamDetailData parsed;
        char parse_error[256];
        if (sc_core_parse_param_result(&result, &parsed, parse_error, sizeof(parse_error))) {
            sc_cli_print_parsed_param_detail(&parsed);
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

int sc_cli_command_reboot_bootloader(int argc, char *argv[])
{
    CliSelectors selectors;
    const char *manifest_path = NULL;
    const char *artifact_path = NULL;
    if (!sc_cli_parse_reboot_args(argc, argv, &selectors,
                                  &manifest_path, &artifact_path)) {
        return 1;
    }

    /* Phase 4 preflight (optional). The manifest verifies the artifact
     * SHA-256 and module-name match before we hand the firmware over to
     * the boot ROM. Hard-reject on any mismatch - the doc requires the
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
    const int idx = sc_cli_select_target_module(&core, &selectors,
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
        fprintf(stderr, "[ERROR] auth: %s - %s\n",
                sc_auth_status_name(auth_st), err);
        return 5;
    }
    printf("[OK] authenticated session on %s\n", target->port_path);

    err[0] = '\0';
    const ScRebootStatus reboot_st = sc_core_reboot_to_bootloader(
        &core.transport, target->port_path, err, sizeof(err));
    if (reboot_st != SC_REBOOT_OK) {
        fprintf(stderr, "[ERROR] reboot: %s - %s\n",
                sc_reboot_status_name(reboot_st), err);
        return 6;
    }

    printf("[OK] firmware acknowledged SC_REBOOT_BOOTLOADER on %s.\n",
           target->port_path);
    printf("Port should disappear shortly; pick up the BOOTSEL/UF2 device "
           "from there (Phase 6).\n");
    return 0;
}

/* ── Phase 8.5 — parameter staging subcommands ────────────────────── */

/* Shared boilerplate: detect modules, resolve target by selectors,
 * authenticate. On success returns 0 and writes the resolved port path
 * into @p out_port_path; on failure writes a diagnostic to stderr and
 * returns the appropriate exit code (1/2/4/5). */
static int phase8_detect_select_authenticate(
    const CliSelectors *selectors,
    ScCore *core,
    char *out_port_path,
    size_t out_port_size)
{
    char detection_log[CLI_DETECTION_LOG_MAX];
    if (!run_detection(core, detection_log, sizeof(detection_log))) {
        fprintf(stderr, "[ERROR] Detection initialization failed.\n");
        return 2;
    }

    char selection_error[256];
    selection_error[0] = '\0';
    const int idx = sc_cli_select_target_module(core, selectors,
                                                selection_error,
                                                sizeof(selection_error));
    if (idx < 0) {
        fprintf(stderr, "[ERROR] %s\n", selection_error);
        return 4;
    }

    const ScModuleStatus *target = sc_core_module_status(core, (size_t)idx);
    if (target == NULL || !target->detected) {
        fprintf(stderr, "[ERROR] Selected module is not detected.\n");
        return 4;
    }
    snprintf(out_port_path, out_port_size, "%s", target->port_path);

    char err[512];
    err[0] = '\0';
    const ScAuthStatus auth_st = sc_core_authenticate(&core->transport,
                                                      out_port_path,
                                                      err, sizeof(err));
    if (auth_st != SC_AUTH_OK) {
        fprintf(stderr, "[ERROR] auth: %s - %s\n",
                sc_auth_status_name(auth_st), err);
        return 5;
    }
    printf("[OK] authenticated session on %s\n", out_port_path);
    return 0;
}

int sc_cli_command_set_param(int argc, char *argv[])
{
    CliSelectors selectors;
    const char *param_id = NULL;
    int value = 0;
    if (!sc_cli_parse_set_param_args(argc, argv, &param_id, &value, &selectors)) {
        return 1;
    }

    ScCore core;
    char port_path[SC_PORT_PATH_MAX];
    const int rc = phase8_detect_select_authenticate(&selectors, &core,
                                                     port_path, sizeof(port_path));
    if (rc != 0) {
        return rc;
    }

    char err[512];
    err[0] = '\0';
    const ScSetParamStatus st = sc_core_set_param(
        &core.transport, port_path, param_id, (int16_t)value,
        err, sizeof(err));
    if (st != SC_SET_PARAM_OK) {
        fprintf(stderr, "[ERROR] set-param: %s - %s\n",
                sc_set_param_status_name(st), err);
        return 6;
    }

    printf("[OK] %s staged %s=%d on %s. Run `commit-params` to apply.\n",
           SC_CMD_SET_PARAM, param_id, value, port_path);
    return 0;
}

int sc_cli_command_commit_params(int argc, char *argv[])
{
    CliSelectors selectors;
    if (!sc_cli_parse_selectors(argc, argv, 2, &selectors)) {
        return 1;
    }

    ScCore core;
    char port_path[SC_PORT_PATH_MAX];
    const int rc = phase8_detect_select_authenticate(&selectors, &core,
                                                     port_path, sizeof(port_path));
    if (rc != 0) {
        return rc;
    }

    char err[512];
    err[0] = '\0';
    const ScCommitParamsStatus st = sc_core_commit_params(
        &core.transport, port_path, err, sizeof(err));
    if (st != SC_COMMIT_PARAMS_OK) {
        fprintf(stderr, "[ERROR] commit-params: %s - %s\n",
                sc_commit_params_status_name(st), err);
        return 6;
    }

    printf("[OK] %s on %s. Active mirror updated; blob persisted.\n",
           SC_CMD_COMMIT_PARAMS, port_path);
    return 0;
}

int sc_cli_command_revert_params(int argc, char *argv[])
{
    CliSelectors selectors;
    if (!sc_cli_parse_selectors(argc, argv, 2, &selectors)) {
        return 1;
    }

    ScCore core;
    char port_path[SC_PORT_PATH_MAX];
    const int rc = phase8_detect_select_authenticate(&selectors, &core,
                                                     port_path, sizeof(port_path));
    if (rc != 0) {
        return rc;
    }

    char err[512];
    err[0] = '\0';
    const ScRevertParamsStatus st = sc_core_revert_params(
        &core.transport, port_path, err, sizeof(err));
    if (st != SC_REVERT_PARAMS_OK) {
        fprintf(stderr, "[ERROR] revert-params: %s - %s\n",
                sc_revert_params_status_name(st), err);
        return 6;
    }

    printf("[OK] %s on %s. Staging mirror reset from active.\n",
           SC_CMD_REVERT_PARAMS, port_path);
    return 0;
}

int sc_cli_command_set_and_commit(int argc, char *argv[])
{
    CliSelectors selectors;
    const char *param_id = NULL;
    int value = 0;
    if (!sc_cli_parse_set_param_args(argc, argv, &param_id, &value, &selectors)) {
        return 1;
    }

    ScCore core;
    char port_path[SC_PORT_PATH_MAX];
    const int rc = phase8_detect_select_authenticate(&selectors, &core,
                                                     port_path, sizeof(port_path));
    if (rc != 0) {
        return rc;
    }

    /* SET. */
    char err[512];
    err[0] = '\0';
    const ScSetParamStatus set_st = sc_core_set_param(
        &core.transport, port_path, param_id, (int16_t)value,
        err, sizeof(err));
    if (set_st != SC_SET_PARAM_OK) {
        fprintf(stderr, "[ERROR] set-param: %s - %s\n",
                sc_set_param_status_name(set_st), err);
        return 6;
    }

    /* COMMIT. On failure roll the staging mirror back via REVERT so
     * the firmware never stays half-mutated. */
    err[0] = '\0';
    const ScCommitParamsStatus commit_st = sc_core_commit_params(
        &core.transport, port_path, err, sizeof(err));
    if (commit_st != SC_COMMIT_PARAMS_OK) {
        fprintf(stderr, "[ERROR] commit-params: %s - %s\n",
                sc_commit_params_status_name(commit_st), err);

        char revert_err[512];
        revert_err[0] = '\0';
        const ScRevertParamsStatus rv_st = sc_core_revert_params(
            &core.transport, port_path, revert_err, sizeof(revert_err));
        if (rv_st == SC_REVERT_PARAMS_OK) {
            fprintf(stderr,
                    "[INFO] auto-reverted staging on %s after commit failure.\n",
                    port_path);
        } else {
            fprintf(stderr,
                    "[WARN] auto-revert also failed: %s - %s. "
                    "Staging may be left mutated until next HELLO.\n",
                    sc_revert_params_status_name(rv_st), revert_err);
        }
        return 6;
    }

    printf("[OK] %s=%d staged + committed on %s.\n",
           param_id, value, port_path);
    return 0;
}
