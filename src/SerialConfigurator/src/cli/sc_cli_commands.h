#ifndef SC_CLI_COMMANDS_H
#define SC_CLI_COMMANDS_H

/*
 * CLI command handlers - one entry point per top-level subcommand.
 * Each handler is responsible for: detection (via run_detection),
 * target selection (via sc_cli_select_target_module from
 * sc_cli_selectors), running the actual sc_core operation, and
 * rendering the result via sc_cli_output.
 *
 * Exit codes (stable for shell pipelines / VS Code tasks):
 *   0  - success
 *   1  - argument parsing failure (caller prints usage)
 *   2  - detection initialisation failure
 *   3  - manifest preflight failure (reboot-bootloader only)
 *   4  - target selection failure
 *   5  - auth or transport failure (reboot-bootloader: auth;
 *        meta_values_or_catalog: device returned non-OK status)
 *   6  - the device refused the operation (reboot ACK, parameter
 *        staging, functional tests)
 */

#include "../config.h"
#include "sc_cli_selectors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLI_DETECTION_LOG_MAX SC_RUNTIME_DETECTION_LOG_MAX
#define CLI_COMMAND_LOG_MAX SC_RUNTIME_COMMAND_LOG_MAX

int sc_cli_command_detect(void);
int sc_cli_command_list(void);

/**
 * @brief Generic handler that dispatches on @p command name to one
 *        of `meta` / `get-values` / `param-list` / `get-param`.
 *        @p param_id is required only for `get-param` and is ignored
 *        otherwise (pass 0).
 */
int sc_cli_command_meta_values_or_catalog(const char *command,
                                          const char *param_id,
                                          const CliSelectors *selectors);

int sc_cli_command_reboot_bootloader(int argc, char *argv[]);

/* Phase 8.5 - auth-gated parameter staging subcommands. Each does its
 * own detection + selection + sc_core_authenticate + the operation.
 * `set-and-commit` chains SET + COMMIT under one auth and on
 * COMMIT failure issues an automatic REVERT so staging is never left
 * in a stale state. Exit codes follow the convention above:
 *   5 - auth or transport, 6 - the sc_core operation returned non-OK. */
int sc_cli_command_set_param(int argc, char *argv[]);
int sc_cli_command_commit_params(int argc, char *argv[]);
int sc_cli_command_revert_params(int argc, char *argv[]);
int sc_cli_command_set_and_commit(int argc, char *argv[]);

/**
 * @brief Send `SC_GET_GPS` and pretty-print the decoded snapshot.
 *
 * Uses the standard --module/--uid/--port selectors. Exits with code
 * 5 when the device returns a non-OK reply (no fix at the time of
 * the request still counts as OK with `available=0`).
 */
int sc_cli_command_get_gps(int argc, char *argv[]);

/*
 * Functional tests of the ECU (SC_TEST_* commands). test-list and
 * test-status only read. test-set, test-run, test-stop and test-skip
 * authenticate first. test-run follows the test until the ECU reports it
 * finished, printing progress; the ECU stops a configurator test whose
 * host goes quiet, so the command keeps polling until the end and Ctrl-C
 * asks the ECU to stop instead of leaving the test behind. Exit codes:
 *   5 - status read failed, 6 - the ECU refused the request, the test did
 *   not start, or a test ended with a result other than done/ok.
 */
int sc_cli_command_test_list(int argc, char *argv[]);
int sc_cli_command_test_status(int argc, char *argv[]);
int sc_cli_command_test_set(int argc, char *argv[]);
int sc_cli_command_test_run(int argc, char *argv[]);
int sc_cli_command_test_stop(int argc, char *argv[]);
int sc_cli_command_test_skip(int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* SC_CLI_COMMANDS_H */
