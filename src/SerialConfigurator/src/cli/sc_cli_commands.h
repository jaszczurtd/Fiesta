#ifndef SC_CLI_COMMANDS_H
#define SC_CLI_COMMANDS_H

/*
 * CLI command handlers — one entry point per top-level subcommand.
 * Each handler is responsible for: detection (via run_detection),
 * target selection (via sc_cli_select_target_module from
 * sc_cli_selectors), running the actual sc_core operation, and
 * rendering the result via sc_cli_output.
 *
 * Exit codes (stable for shell pipelines / VS Code tasks):
 *   0  — success
 *   1  — argument parsing failure (caller prints usage)
 *   2  — detection initialisation failure
 *   3  — manifest preflight failure (reboot-bootloader only)
 *   4  — target selection failure
 *   5  — auth or transport failure (reboot-bootloader: auth;
 *        meta_values_or_catalog: device returned non-OK status)
 *   6  — reboot ACK failure (reboot-bootloader only)
 */

#include "sc_cli_selectors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLI_DETECTION_LOG_MAX 16384u
#define CLI_COMMAND_LOG_MAX 4096u

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

#ifdef __cplusplus
}
#endif

#endif /* SC_CLI_COMMANDS_H */
