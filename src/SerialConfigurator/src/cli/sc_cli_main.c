/*
 * CLI process entry. Tiny dispatch over subcommands. Every subcommand
 * lives in a focused module:
 *
 *   sc_cli_selectors.{h,c}  - --module/--uid/--port + per-command
 *                             arg parsers + target-module resolver
 *   sc_cli_output.{h,c}     - stdout printers (module table, parsed
 *                             values / param list / param detail)
 *   sc_cli_commands.{h,c}   - command_detect, command_list,
 *                             command_meta_values_or_catalog,
 *                             command_reboot_bootloader, plus the
 *                             stable exit-code conventions
 *
 * Exit codes documented in sc_cli_commands.h.
 */

#include <stdio.h>
#include <string.h>

#include "sc_cli_commands.h"
#include "sc_cli_selectors.h"

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
    fprintf(stderr, "  %s set-param --id <param_id> --value <i16> "
                    "[--module <name>] [--uid <hex>] [--port <path>]\n",
            program_name);
    fprintf(stderr, "  %s commit-params [--module <name>] [--uid <hex>] [--port <path>]\n",
            program_name);
    fprintf(stderr, "  %s revert-params [--module <name>] [--uid <hex>] [--port <path>]\n",
            program_name);
    fprintf(stderr, "  %s set-and-commit --id <param_id> --value <i16> "
                    "[--module <name>] [--uid <hex>] [--port <path>]\n",
            program_name);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *command = argv[1];

    if (strcmp(command, "detect") == 0) {
        return sc_cli_command_detect();
    }

    if (strcmp(command, "reboot-bootloader") == 0) {
        return sc_cli_command_reboot_bootloader(argc, argv);
    }

    if (strcmp(command, "list") == 0) {
        return sc_cli_command_list();
    }

    if (strcmp(command, "meta") == 0 ||
        strcmp(command, "get-values") == 0 ||
        strcmp(command, "param-list") == 0) {
        CliSelectors selectors;
        if (!sc_cli_parse_selectors(argc, argv, 2, &selectors)) {
            print_usage(argv[0]);
            return 1;
        }

        return sc_cli_command_meta_values_or_catalog(
            command,
            0,
            &selectors
        );
    }

    if (strcmp(command, "set-param") == 0) {
        return sc_cli_command_set_param(argc, argv);
    }

    if (strcmp(command, "commit-params") == 0) {
        return sc_cli_command_commit_params(argc, argv);
    }

    if (strcmp(command, "revert-params") == 0) {
        return sc_cli_command_revert_params(argc, argv);
    }

    if (strcmp(command, "set-and-commit") == 0) {
        return sc_cli_command_set_and_commit(argc, argv);
    }

    if (strcmp(command, "get-param") == 0) {
        CliSelectors selectors;
        const char *param_id = 0;
        if (!sc_cli_parse_get_param_args(argc, argv, &param_id, &selectors)) {
            print_usage(argv[0]);
            return 1;
        }

        return sc_cli_command_meta_values_or_catalog(
            "get-param",
            param_id,
            &selectors
        );
    }

    fprintf(stderr, "[ERROR] Unknown command: %s\n", command);
    print_usage(argv[0]);
    return 1;
}
