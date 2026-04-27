#include "sc_cli_selectors.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

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

bool sc_cli_parse_selectors(int argc,
                            char *argv[],
                            int start_index,
                            CliSelectors *selectors)
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

bool sc_cli_parse_get_param_args(int argc,
                                 char *argv[],
                                 const char **param_id,
                                 CliSelectors *selectors)
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

bool sc_cli_parse_reboot_args(int argc,
                              char *argv[],
                              CliSelectors *selectors,
                              const char **manifest_path,
                              const char **artifact_path)
{
    if (selectors == 0 || manifest_path == 0 || artifact_path == 0) {
        return false;
    }

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

bool sc_cli_module_matches_selectors(const ScModuleStatus *status,
                                     const CliSelectors *selectors)
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

int sc_cli_select_target_module(const ScCore *core,
                                const CliSelectors *selectors,
                                char *error,
                                size_t error_size)
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

        if (has_selector && !sc_cli_module_matches_selectors(status, selectors)) {
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
