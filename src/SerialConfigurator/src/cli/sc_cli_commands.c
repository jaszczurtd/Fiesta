#include "sc_cli_commands.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sc_cli_output.h"
#include "sc_core.h"
#include "sc_gps.h"
#include "sc_manifest.h"
#include "sc_tests.h"
#include "sc_text.h"
#include "sc_time.h"

static bool run_detection(ScCore *core, char *log, size_t log_size) {
  if (core == 0 || log == 0 || log_size == 0u) {
    return false;
  }

  sc_core_init(core);
  sc_core_detect_modules(core, log, log_size);
  return true;
}

/* Detect modules and resolve the single target. Returns 0 with the module
 * index, or the exit code: 2 detection, 4 selection (the module table is
 * printed so the operator sees what was found). */
static int detect_and_select(const CliSelectors *selectors, ScCore *core,
                             size_t *out_index) {
  char detection_log[CLI_DETECTION_LOG_MAX];
  if (!run_detection(core, detection_log, sizeof(detection_log))) {
    fprintf(stderr, "[ERROR] Detection initialization failed.\n");
    return 2;
  }

  char selection_error[256];
  selection_error[0] = '\0';
  const int idx = sc_cli_select_target_module(core, selectors, selection_error,
                                              sizeof(selection_error));
  if (idx < 0) {
    fprintf(stderr, "[ERROR] %s\n", selection_error);
    sc_cli_print_module_table(core);
    return 4;
  }
  *out_index = (size_t)idx;
  return 0;
}

int sc_cli_command_detect(void) {
  ScCore core;
  char detection_log[CLI_DETECTION_LOG_MAX];

  if (!run_detection(&core, detection_log, sizeof(detection_log))) {
    fprintf(stderr, "[ERROR] Detection initialization failed.\n");
    return 2;
  }

  printf("%s", detection_log);
  return 0;
}

int sc_cli_command_list(void) {
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
                                          const CliSelectors *selectors) {
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
      &core, selectors, selection_error, sizeof(selection_error));
  if (module_index < 0) {
    fprintf(stderr, "[ERROR] %s\n", selection_error);
    sc_cli_print_module_table(&core);
    return 3;
  }

  ScCommandResult result;
  bool ok = false;
  if (strcmp(command, "meta") == 0) {
    ok = sc_core_sc_get_meta(&core, (size_t)module_index, &result, command_log,
                             sizeof(command_log));
  } else if (strcmp(command, "get-values") == 0) {
    ok = sc_core_sc_get_values(&core, (size_t)module_index, &result,
                               command_log, sizeof(command_log));
  } else if (strcmp(command, "param-list") == 0) {
    ok = sc_core_sc_get_param_list(&core, (size_t)module_index, &result,
                                   command_log, sizeof(command_log));
  } else if (strcmp(command, "get-param") == 0) {
    ok = sc_core_sc_get_param(&core, (size_t)module_index, param_id, &result,
                              command_log, sizeof(command_log));
  }

  if (!ok) {
    fprintf(stderr, "[ERROR] Command transport failed.\n");
    fprintf(stderr, "%s", command_log);
    return 4;
  }

  printf("%s\n", result.response);

  if (strcmp(command, "meta") == 0 && result.status == SC_COMMAND_STATUS_OK) {
    const ScModuleStatus *status =
        sc_core_module_status(&core, (size_t)module_index);
    if (status != 0 && status->meta_identity.valid) {
      printf("PARSED module=%s proto=%s session=%s fw=%s build=%s uid=%s\n",
             sc_cli_value_or_dash(status->meta_identity.module_name),
             status->meta_identity.proto_present ? "set" : "-",
             status->meta_identity.session_present ? "set" : "-",
             sc_cli_value_or_dash(status->meta_identity.fw_version),
             sc_cli_value_or_dash(status->meta_identity.build_id),
             sc_cli_value_or_dash(status->meta_identity.uid));
    }
  }

  if (strcmp(command, "param-list") == 0 &&
      result.status == SC_COMMAND_STATUS_OK) {
    ScParamListData parsed;
    char parse_error[256];
    if (sc_core_parse_param_list_result(&result, &parsed, parse_error,
                                        sizeof(parse_error))) {
      sc_cli_print_parsed_param_list(&parsed);
    } else {
      fprintf(stderr, "[WARN] %s\n", parse_error);
    }
  }

  if (strcmp(command, "get-values") == 0 &&
      result.status == SC_COMMAND_STATUS_OK) {
    ScParamValuesData parsed;
    char parse_error[256];
    if (sc_core_parse_param_values_result(&result, &parsed, parse_error,
                                          sizeof(parse_error))) {
      sc_cli_print_parsed_values(&parsed);
    } else {
      fprintf(stderr, "[WARN] %s\n", parse_error);
    }
  }

  if (strcmp(command, "get-param") == 0 &&
      result.status == SC_COMMAND_STATUS_OK) {
    ScParamDetailData parsed;
    char parse_error[256];
    if (sc_core_parse_param_result(&result, &parsed, parse_error,
                                   sizeof(parse_error))) {
      sc_cli_print_parsed_param_detail(&parsed);
    } else {
      fprintf(stderr, "[WARN] %s\n", parse_error);
    }
  }

  if (result.status != SC_COMMAND_STATUS_OK) {
    fprintf(stderr, "[ERROR] Device returned non-OK status: %s\n",
            sc_command_status_name(result.status));
    return 5;
  }

  return 0;
}

int sc_cli_command_reboot_bootloader(int argc, char *argv[]) {
  CliSelectors selectors;
  const char *manifest_path = NULL;
  const char *artifact_path = NULL;
  if (!sc_cli_parse_reboot_args(argc, argv, &selectors, &manifest_path,
                                &artifact_path)) {
    return 1;
  }

  /* Phase 4 preflight (optional). The manifest verifies the artifact
   * SHA-256 and module-name match before we hand the firmware over to
   * the boot ROM. Hard-reject on any mismatch - the doc requires the
   * flashing flow to fail closed. */
  sc_manifest_t manifest;
  bool have_manifest = false;
  char artifact_from_manifest[512];
  artifact_from_manifest[0] = '\0';
  const char *effective_artifact_path = artifact_path;
  if (manifest_path != NULL) {
    const sc_manifest_status_t st =
        sc_manifest_load_file(manifest_path, &manifest);
    if (st != SC_MANIFEST_OK) {
      fprintf(stderr, "[ERROR] manifest load: %s\n",
              sc_manifest_status_str(st));
      return 3;
    }
    have_manifest = true;

    if (effective_artifact_path == NULL) {
      const sc_manifest_status_t rp = sc_manifest_resolve_uf2_path(
          manifest_path, &manifest, artifact_from_manifest,
          sizeof(artifact_from_manifest));
      if (rp == SC_MANIFEST_OK) {
        effective_artifact_path = artifact_from_manifest;
        printf("[OK] artifact path resolved from manifest: %s\n",
               effective_artifact_path);
      } else if (rp != SC_MANIFEST_ERR_UF2_FILE_MISSING) {
        fprintf(stderr, "[ERROR] manifest uf2_file resolve failed: %s\n",
                sc_manifest_status_str(rp));
        return 3;
      }
    }

    if (effective_artifact_path != NULL) {
      const sc_manifest_status_t av =
          sc_manifest_verify_artifact(&manifest, effective_artifact_path);
      if (av != SC_MANIFEST_OK) {
        fprintf(stderr, "[ERROR] artifact verify: %s\n",
                sc_manifest_status_str(av));
        return 3;
      }
      printf("[OK] manifest sha256 matches artifact %s\n",
             effective_artifact_path);
    } else {
      fprintf(stderr, "[WARN] --manifest provided without --artifact; "
                      "manifest fields parsed but SHA-256 not verified "
                      "(no uf2_file in manifest).\n");
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
  const int idx = sc_cli_select_target_module(
      &core, &selectors, selection_error, sizeof(selection_error));
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
    printf("[OK] manifest module=%s matches target.\n", target->display_name);
  }

  char err[512];
  err[0] = '\0';
  const ScAuthStatus auth_st = sc_core_authenticate(
      &core.transport, target->port_path, err, sizeof(err));
  if (auth_st != SC_AUTH_OK) {
    fprintf(stderr, "[ERROR] auth: %s - %s\n", sc_auth_status_name(auth_st),
            err);
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

/* ── Phase 8.5 - parameter staging subcommands ────────────────────── */

/* Shared boilerplate: detect modules, resolve target by selectors,
 * authenticate. On success returns 0 and writes the resolved port path
 * into @p out_port_path (and the module index when @p out_index is not
 * NULL); on failure writes a diagnostic to stderr and returns the
 * appropriate exit code (2/4/5). */
static int detect_select_authenticate(const CliSelectors *selectors,
                                      ScCore *core, size_t *out_index,
                                      char *out_port_path,
                                      size_t out_port_size) {
  size_t index = 0u;
  const int rc = detect_and_select(selectors, core, &index);
  if (rc != 0) {
    return rc;
  }

  const ScModuleStatus *target = sc_core_module_status(core, index);
  if (target == NULL || !target->detected) {
    fprintf(stderr, "[ERROR] Selected module is not detected.\n");
    return 4;
  }
  snprintf(out_port_path, out_port_size, "%s", target->port_path);
  if (out_index != NULL) {
    *out_index = index;
  }

  char err[512];
  err[0] = '\0';
  const ScAuthStatus auth_st =
      sc_core_authenticate(&core->transport, out_port_path, err, sizeof(err));
  if (auth_st != SC_AUTH_OK) {
    fprintf(stderr, "[ERROR] auth: %s - %s\n", sc_auth_status_name(auth_st),
            err);
    return 5;
  }
  printf("[OK] authenticated session on %s\n", out_port_path);
  return 0;
}

/* Parses `--id/--value` plus selectors, authenticates the selected module
 * and stages the value with SET. Returns 0 or the command's exit code. */
static int stage_param(int argc, char *argv[], ScCore *core, char *port_path,
                       size_t port_path_size, const char **param_id,
                       int *value) {
  CliSelectors selectors;
  if (!sc_cli_parse_set_param_args(argc, argv, param_id, value, &selectors)) {
    return 1;
  }

  const int rc = detect_select_authenticate(&selectors, core, NULL, port_path,
                                            port_path_size);
  if (rc != 0) {
    return rc;
  }

  char err[512];
  err[0] = '\0';
  const ScSetParamStatus st =
      sc_core_set_param(&core->transport, port_path, *param_id, (int16_t)*value,
                        err, sizeof(err));
  if (st != SC_SET_PARAM_OK) {
    fprintf(stderr, "[ERROR] set-param: %s - %s\n",
            sc_set_param_status_name(st), err);
    return 6;
  }
  return 0;
}

int sc_cli_command_set_param(int argc, char *argv[]) {
  ScCore core;
  char port_path[SC_PORT_PATH_MAX];
  const char *param_id = NULL;
  int value = 0;
  const int rc = stage_param(argc, argv, &core, port_path, sizeof(port_path),
                             &param_id, &value);
  if (rc != 0) {
    return rc;
  }

  printf("[OK] %s staged %s=%d on %s. Run `commit-params` to apply.\n",
         SC_CMD_SET_PARAM, param_id, value, port_path);
  return 0;
}

int sc_cli_command_commit_params(int argc, char *argv[]) {
  CliSelectors selectors;
  if (!sc_cli_parse_selectors(argc, argv, 2, &selectors)) {
    return 1;
  }

  ScCore core;
  char port_path[SC_PORT_PATH_MAX];
  const int rc = detect_select_authenticate(&selectors, &core, NULL, port_path,
                                            sizeof(port_path));
  if (rc != 0) {
    return rc;
  }

  char err[512];
  err[0] = '\0';
  const ScCommitParamsStatus st =
      sc_core_commit_params(&core.transport, port_path, err, sizeof(err));
  if (st != SC_COMMIT_PARAMS_OK) {
    fprintf(stderr, "[ERROR] commit-params: %s - %s\n",
            sc_commit_params_status_name(st), err);
    return 6;
  }

  printf("[OK] %s on %s. Active mirror updated; blob persisted.\n",
         SC_CMD_COMMIT_PARAMS, port_path);
  return 0;
}

int sc_cli_command_revert_params(int argc, char *argv[]) {
  CliSelectors selectors;
  if (!sc_cli_parse_selectors(argc, argv, 2, &selectors)) {
    return 1;
  }

  ScCore core;
  char port_path[SC_PORT_PATH_MAX];
  const int rc = detect_select_authenticate(&selectors, &core, NULL, port_path,
                                            sizeof(port_path));
  if (rc != 0) {
    return rc;
  }

  char err[512];
  err[0] = '\0';
  const ScRevertParamsStatus st =
      sc_core_revert_params(&core.transport, port_path, err, sizeof(err));
  if (st != SC_REVERT_PARAMS_OK) {
    fprintf(stderr, "[ERROR] revert-params: %s - %s\n",
            sc_revert_params_status_name(st), err);
    return 6;
  }

  printf("[OK] %s on %s. Staging mirror reset from active.\n",
         SC_CMD_REVERT_PARAMS, port_path);
  return 0;
}

int sc_cli_command_set_and_commit(int argc, char *argv[]) {
  ScCore core;
  char port_path[SC_PORT_PATH_MAX];
  const char *param_id = NULL;
  int value = 0;
  const int rc = stage_param(argc, argv, &core, port_path, sizeof(port_path),
                             &param_id, &value);
  if (rc != 0) {
    return rc;
  }

  /* COMMIT. On failure roll the staging mirror back via REVERT so
   * the firmware never stays half-mutated. */
  char err[512];
  err[0] = '\0';
  const ScCommitParamsStatus commit_st =
      sc_core_commit_params(&core.transport, port_path, err, sizeof(err));
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

  printf("[OK] %s=%d staged + committed on %s.\n", param_id, value, port_path);
  return 0;
}

int sc_cli_command_get_gps(int argc, char *argv[]) {
  CliSelectors selectors;
  if (!sc_cli_parse_selectors(argc, argv, 2, &selectors)) {
    return 1;
  }

  ScCore core;
  size_t module_index = 0u;
  const int rc = detect_and_select(&selectors, &core, &module_index);
  if (rc != 0) {
    return rc;
  }

  char command_log[CLI_COMMAND_LOG_MAX];
  command_log[0] = '\0';
  ScCommandResult result;
  if (!sc_gps_get(&core, module_index, &result, command_log,
                  sizeof(command_log))) {
    fprintf(stderr, "[ERROR] Command transport failed.\n");
    fprintf(stderr, "%s", command_log);
    return 4;
  }

  printf("%s\n", result.response);

  if (result.status == SC_COMMAND_STATUS_OK) {
    ScGpsSnapshot snapshot;
    char parse_error[256];
    if (sc_gps_parse_result(&result, &snapshot, parse_error,
                            sizeof(parse_error))) {
      sc_cli_print_gps_snapshot(&snapshot);
    } else {
      fprintf(stderr, "[WARN] %s\n", parse_error);
    }
  }

  if (result.status != SC_COMMAND_STATUS_OK) {
    fprintf(stderr, "[ERROR] Device returned non-OK status: %s\n",
            sc_command_status_name(result.status));
    return 5;
  }

  return 0;
}

/* ── Functional tests ─────────────────────────────────────────────── */

int sc_cli_command_test_list(int argc, char *argv[]) {
  CliSelectors selectors;
  if (!sc_cli_parse_selectors(argc, argv, 2, &selectors)) {
    return 1;
  }
  ScCore core;
  size_t index = 0u;
  const int rc = detect_and_select(&selectors, &core, &index);
  if (rc != 0) {
    return rc;
  }

  static ScTestCatalog catalog;
  char err[512];
  err[0] = '\0';
  if (!sc_tests_load_catalog(&core, index, &catalog, err, sizeof(err))) {
    fprintf(stderr, "[ERROR] test-list: %s\n", err);
    return 5;
  }
  sc_cli_print_test_catalog(&catalog);
  return 0;
}

int sc_cli_command_test_status(int argc, char *argv[]) {
  CliSelectors selectors;
  if (!sc_cli_parse_selectors(argc, argv, 2, &selectors)) {
    return 1;
  }
  ScCore core;
  size_t index = 0u;
  const int rc = detect_and_select(&selectors, &core, &index);
  if (rc != 0) {
    return rc;
  }

  ScTestStatus status;
  char err[512];
  err[0] = '\0';
  if (!sc_tests_get_status(&core, index, &status, err, sizeof(err))) {
    fprintf(stderr, "[ERROR] test-status: %s\n", err);
    return 5;
  }
  char line[512];
  sc_cli_format_test_status(&status, true, line, sizeof(line));
  printf("%s\n", line);
  return 0;
}

int sc_cli_command_test_set(int argc, char *argv[]) {
  CliSelectors selectors;
  const char *param_id = NULL;
  long value = 0;
  if (!sc_cli_parse_id_value_args(argc, argv, INT32_MIN, INT32_MAX, "int32_t",
                                  &param_id, &value, &selectors)) {
    return 1;
  }

  ScCore core;
  char port_path[SC_PORT_PATH_MAX];
  const int rc = detect_select_authenticate(&selectors, &core, NULL, port_path,
                                            sizeof(port_path));
  if (rc != 0) {
    return rc;
  }

  char err[512];
  err[0] = '\0';
  const ScTestActionStatus st = sc_tests_set_param(
      &core.transport, port_path, param_id, (int32_t)value, err, sizeof(err));
  if (st != SC_TEST_ACTION_OK) {
    fprintf(stderr, "[ERROR] test-set: %s - %s\n",
            sc_test_action_status_name(st), err);
    return 6;
  }
  printf("[OK] %s=%ld on %s; it holds until the ECU resets.\n", param_id, value,
         port_path);
  return 0;
}

typedef ScTestActionStatus (*test_action_fn)(const ScTransport *transport,
                                             const char *device_path,
                                             char *error, size_t error_size);

/* test-stop / test-skip: one authenticated request, no arguments. */
static int run_test_action(int argc, char *argv[], const char *label,
                           test_action_fn action) {
  CliSelectors selectors;
  if (!sc_cli_parse_selectors(argc, argv, 2, &selectors)) {
    return 1;
  }
  ScCore core;
  char port_path[SC_PORT_PATH_MAX];
  const int rc = detect_select_authenticate(&selectors, &core, NULL, port_path,
                                            sizeof(port_path));
  if (rc != 0) {
    return rc;
  }

  char err[512];
  err[0] = '\0';
  const ScTestActionStatus st =
      action(&core.transport, port_path, err, sizeof(err));
  if (st != SC_TEST_ACTION_OK) {
    fprintf(stderr, "[ERROR] %s: %s - %s\n", label,
            sc_test_action_status_name(st), err);
    return 6;
  }
  printf("[OK] %s on %s\n", label, port_path);
  return 0;
}

int sc_cli_command_test_stop(int argc, char *argv[]) {
  return run_test_action(argc, argv, "test-stop", sc_tests_stop);
}

int sc_cli_command_test_skip(int argc, char *argv[]) {
  return run_test_action(argc, argv, "test-skip", sc_tests_skip);
}

static volatile sig_atomic_t s_test_interrupted = 0;

static void on_test_interrupt(int signo) {
  (void)signo;
  s_test_interrupted = 1;
}

static bool test_result_passed(const char *result) {
  return strcmp(result, SC_TEST_RESULT_DONE) == 0 ||
         strcmp(result, SC_TEST_RESULT_OK) == 0;
}

/* Follow a started test until the ECU reports it finished. Polling is what
 * keeps a configurator test alive, so this loop runs until the end; Ctrl-C
 * asks the ECU to stop and keeps following until it has. */
static int follow_test_run(ScCore *core, size_t index, const char *port_path,
                           const char *name, const ScTestStatus *before) {
  char previous_last[SC_TEST_ID_MAX];
  char previous_result[SC_TESTS_TOKEN_MAX];
  sc_text_copy(previous_last, sizeof(previous_last),
               before->has_last ? before->last : "");
  sc_text_copy(previous_result, sizeof(previous_result), before->result);

  const uint64_t started_ms = sc_time_monotonic_ms();
  uint64_t printed_ms = 0u;
  char printed_test[SC_TEST_ID_MAX] = "";
  unsigned printed_seq = 0u;
  unsigned results = 0u;
  bool failed = false;
  bool stop_sent = false;
  unsigned read_failures = 0u;
  char err[512];

  for (;;) {
    sc_time_sleep_ms(SC_TESTS_POLL_INTERVAL_MS);
    if (s_test_interrupted != 0 && !stop_sent) {
      err[0] = '\0';
      const ScTestActionStatus st =
          sc_tests_stop(&core->transport, port_path, err, sizeof(err));
      stop_sent = st == SC_TEST_ACTION_OK;
      if (stop_sent) {
        printf("[INFO] stop requested\n");
      } else {
        fprintf(stderr, "[WARN] stop: %s - %s\n",
                sc_test_action_status_name(st), err);
      }
    }

    ScTestStatus status;
    err[0] = '\0';
    if (!sc_tests_get_status(core, index, &status, err, sizeof(err))) {
      if (++read_failures >= 3u) {
        fprintf(stderr, "[ERROR] test-status: %s\n", err);
        return 5;
      }
      continue;
    }
    read_failures = 0u;
    const uint64_t now_ms = sc_time_monotonic_ms();

    if (status.runs == before->runs) {
      if (now_ms - started_ms > SC_TESTS_START_TIMEOUT_MS) {
        fprintf(stderr, "[ERROR] %s did not start.\n", name);
        return 6;
      }
      continue;
    }

    const bool new_result =
        status.has_last && (strcmp(status.last, previous_last) != 0 ||
                            strcmp(status.result, previous_result) != 0);
    if (new_result || (!status.running && results == 0u && status.has_last)) {
      printf("[RESULT] %s: %s\n", status.last, status.result);
      failed = failed || !test_result_passed(status.result);
      results++;
      sc_text_copy(previous_last, sizeof(previous_last), status.last);
      sc_text_copy(previous_result, sizeof(previous_result), status.result);
    }

    if (!status.running) {
      return failed ? 6 : 0;
    }

    /* Once a second, and at once when the test or sequence step changes. */
    if (strcmp(status.test, printed_test) != 0 ||
        status.seq_index != printed_seq || now_ms - printed_ms >= 1000u) {
      char line[512];
      sc_cli_format_test_status(&status, true, line, sizeof(line));
      printf("%s\n", line);
      fflush(stdout);
      sc_text_copy(printed_test, sizeof(printed_test), status.test);
      printed_seq = status.seq_index;
      printed_ms = now_ms;
    }
  }
}

int sc_cli_command_test_run(int argc, char *argv[]) {
  CliSelectors selectors;
  const char *name = NULL;
  if (!sc_cli_parse_positional_args(argc, argv, "<test|" SC_TEST_SEQUENCE ">",
                                    &name, &selectors)) {
    return 1;
  }

  ScCore core;
  size_t index = 0u;
  char port_path[SC_PORT_PATH_MAX];
  const int rc = detect_select_authenticate(&selectors, &core, &index,
                                            port_path, sizeof(port_path));
  if (rc != 0) {
    return rc;
  }

  ScTestStatus before;
  char err[512];
  err[0] = '\0';
  if (!sc_tests_get_status(&core, index, &before, err, sizeof(err))) {
    fprintf(stderr, "[ERROR] test-status: %s\n", err);
    return 5;
  }
  const ScTestActionStatus st =
      sc_tests_run(&core.transport, port_path, name, err, sizeof(err));
  if (st != SC_TEST_ACTION_OK) {
    fprintf(stderr, "[ERROR] test-run: %s - %s\n",
            sc_test_action_status_name(st), err);
    return 6;
  }
  printf("[OK] %s started on %s; Ctrl-C stops it.\n", name, port_path);
  fflush(stdout);

  struct sigaction action;
  struct sigaction previous;
  memset(&action, 0, sizeof(action));
  action.sa_handler = on_test_interrupt;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  s_test_interrupted = 0;
  (void)sigaction(SIGINT, &action, &previous);
  const int result = follow_test_run(&core, index, port_path, name, &before);
  (void)sigaction(SIGINT, &previous, NULL);
  return result;
}
