#ifndef SC_CLI_SELECTORS_H
#define SC_CLI_SELECTORS_H

/*
 * CLI selector parsing and target-module resolution.
 *
 * Every command that operates on a single target module accepts the
 * same `--module` / `--uid` / `--port` triple. This module owns the
 * struct, the per-command parsers (each command has slightly
 * different positional arguments), and the fail-closed resolver that
 * maps the selectors against detected modules.
 */

#include <stdbool.h>
#include <stddef.h>

#include "sc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CliSelectors {
    const char *module;
    const char *uid;
    const char *port;
} CliSelectors;

/**
 * @brief Parse `--module` / `--uid` / `--port` from argv starting at
 *        @p start_index. Used by commands that take only selectors.
 *
 * Writes errors to stderr and returns false on bad/missing args.
 */
bool sc_cli_parse_selectors(int argc,
                            char *argv[],
                            int start_index,
                            CliSelectors *out);

/**
 * @brief Parse `<param-id>` plus selectors for the get-param command.
 *        The first non-`--*` argv slot becomes @p param_id.
 */
bool sc_cli_parse_get_param_args(int argc,
                                 char *argv[],
                                 const char **param_id,
                                 CliSelectors *out);

/**
 * @brief Parse selectors plus optional `--manifest <path>` and
 *        `--artifact <path>` for the reboot-bootloader command.
 */
bool sc_cli_parse_reboot_args(int argc,
                              char *argv[],
                              CliSelectors *out,
                              const char **manifest_path,
                              const char **artifact_path);

/**
 * @brief Parse `--id <param_id> --value <i16>` plus selectors for the
 *        set-param / set-and-commit subcommands.
 *
 * @p value receives the parsed signed integer in the int16_t domain;
 * out-of-range numerics, missing flags, or extra positional args
 * cause this to return false (and write a reason to stderr).
 */
bool sc_cli_parse_set_param_args(int argc,
                                 char *argv[],
                                 const char **param_id,
                                 int *value,
                                 CliSelectors *out);

/**
 * @brief Return true when @p status matches every non-NULL selector
 *        field. Module name match is case-insensitive against both
 *        `display_name` and `hello_identity.module_name`; UID is
 *        case-insensitive; port is exact.
 */
bool sc_cli_module_matches_selectors(const ScModuleStatus *status,
                                     const CliSelectors *selectors);

/**
 * @brief Resolve the single target module index. Fails closed when
 *        zero modules match, multiple modules match, or the matched
 *        module is itself ambiguous (multi-instance). Writes a human-
 *        readable reason into @p error on any failure.
 *
 * @return module index >= 0 on success, -1 on failure.
 */
int sc_cli_select_target_module(const ScCore *core,
                                const CliSelectors *selectors,
                                char *error,
                                size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* SC_CLI_SELECTORS_H */
