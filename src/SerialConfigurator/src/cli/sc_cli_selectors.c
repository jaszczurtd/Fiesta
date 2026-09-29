#include "sc_cli_selectors.h"
#include "sc_text.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* When argv[*index] is option @p name, stores the argument after it in
 * *value and moves *index past both. Returns 1 when consumed, 0 when
 * argv[*index] is another argument, -1 when the value is missing. */
static int take_option(int argc, char *argv[], int *index, const char *name,
                       const char **value) {
  if (strcmp(argv[*index], name) != 0) {
    return 0;
  }
  if (*index + 1 >= argc) {
    fprintf(stderr, "[ERROR] Missing value for %s\n", name);
    return -1;
  }
  *value = argv[*index + 1];
  *index += 2;
  return 1;
}

/* take_option() for `--module`, `--uid` and `--port`. */
static int take_selector(int argc, char *argv[], int *index,
                         CliSelectors *selectors) {
  int taken = take_option(argc, argv, index, "--module", &selectors->module);
  if (taken == 0) {
    taken = take_option(argc, argv, index, "--uid", &selectors->uid);
  }
  if (taken == 0) {
    taken = take_option(argc, argv, index, "--port", &selectors->port);
  }
  return taken;
}

static void clear_selectors(CliSelectors *selectors) {
  selectors->module = NULL;
  selectors->uid = NULL;
  selectors->port = NULL;
}

bool sc_cli_parse_selectors(int argc, char *argv[], int start_index,
                            CliSelectors *selectors) {
  if (selectors == 0) {
    return false;
  }

  clear_selectors(selectors);
  int i = start_index;
  while (i < argc) {
    const int taken = take_selector(argc, argv, &i, selectors);
    if (taken < 0) {
      return false;
    }
    if (taken == 0) {
      fprintf(stderr, "[ERROR] Unknown option: %s\n", argv[i]);
      return false;
    }
  }

  return true;
}

bool sc_cli_parse_positional_args(int argc, char *argv[], const char *label,
                                  const char **param_id,
                                  CliSelectors *selectors) {
  if (label == 0 || param_id == 0 || selectors == 0) {
    return false;
  }

  *param_id = 0;
  clear_selectors(selectors);
  int i = 2;
  while (i < argc) {
    const int taken = take_selector(argc, argv, &i, selectors);
    if (taken < 0) {
      return false;
    }
    if (taken > 0) {
      continue;
    }
    if (*param_id != 0) {
      fprintf(stderr, "[ERROR] Unknown option or extra argument: %s\n",
              argv[i]);
      return false;
    }
    *param_id = argv[i];
    i++;
  }

  if (*param_id == 0 || (*param_id)[0] == '\0') {
    fprintf(stderr, "[ERROR] Missing %s.\n", label);
    return false;
  }

  return true;
}

bool sc_cli_parse_get_param_args(int argc, char *argv[], const char **param_id,
                                 CliSelectors *selectors) {
  return sc_cli_parse_positional_args(argc, argv, "<param-id>", param_id,
                                      selectors);
}

bool sc_cli_parse_reboot_args(int argc, char *argv[], CliSelectors *selectors,
                              const char **manifest_path,
                              const char **artifact_path) {
  if (selectors == 0 || manifest_path == 0 || artifact_path == 0) {
    return false;
  }

  *manifest_path = NULL;
  *artifact_path = NULL;
  clear_selectors(selectors);
  int i = 2;
  while (i < argc) {
    int taken = take_selector(argc, argv, &i, selectors);
    if (taken == 0) {
      taken = take_option(argc, argv, &i, "--manifest", manifest_path);
    }
    if (taken == 0) {
      taken = take_option(argc, argv, &i, "--artifact", artifact_path);
    }
    if (taken < 0) {
      return false;
    }
    if (taken == 0) {
      fprintf(stderr, "[ERROR] Unknown option: %s\n", argv[i]);
      return false;
    }
  }
  return true;
}

/* Strict base-10 parse of @p raw into [@p min_value, @p max_value]. */
static bool parse_ranged_long(const char *raw, long min_value, long max_value,
                              long *value) {
  char *end = NULL;
  errno = 0;
  const long parsed = strtol(raw, &end, 10);
  if (errno != 0 || end == raw || *end != '\0' || parsed < min_value ||
      parsed > max_value) {
    return false;
  }
  *value = parsed;
  return true;
}

bool sc_cli_parse_id_value_args(int argc, char *argv[], long min_value,
                                long max_value, const char *value_label,
                                const char **param_id, long *value,
                                CliSelectors *selectors) {
  if (value_label == NULL || param_id == NULL || value == NULL ||
      selectors == NULL) {
    return false;
  }

  *param_id = NULL;
  *value = 0;
  bool value_set = false;
  clear_selectors(selectors);
  int i = 2;
  while (i < argc) {
    const char *raw = NULL;
    int taken = take_option(argc, argv, &i, "--id", param_id);
    if (taken == 0) {
      taken = take_option(argc, argv, &i, "--value", &raw);
      if (taken > 0) {
        if (!parse_ranged_long(raw, min_value, max_value, value)) {
          fprintf(stderr,
                  "[ERROR] --value must be an %s (%ld..%ld), got '%s'\n",
                  value_label, min_value, max_value, raw);
          return false;
        }
        value_set = true;
      }
    }
    if (taken == 0) {
      taken = take_selector(argc, argv, &i, selectors);
    }
    if (taken < 0) {
      return false;
    }
    if (taken == 0) {
      fprintf(stderr, "[ERROR] Unknown option: %s\n", argv[i]);
      return false;
    }
  }

  if (*param_id == NULL || (*param_id)[0] == '\0') {
    fprintf(stderr, "[ERROR] Missing required --id <param_id>.\n");
    return false;
  }
  if (!value_set) {
    fprintf(stderr, "[ERROR] Missing required --value <%s>.\n", value_label);
    return false;
  }
  return true;
}

bool sc_cli_parse_set_param_args(int argc, char *argv[], const char **param_id,
                                 int *value, CliSelectors *selectors) {
  long parsed = 0;
  if (value == NULL ||
      !sc_cli_parse_id_value_args(argc, argv, INT16_MIN, INT16_MAX, "int16_t",
                                  param_id, &parsed, selectors)) {
    return false;
  }
  *value = (int)parsed;
  return true;
}

bool sc_cli_module_matches_selectors(const ScModuleStatus *status,
                                     const CliSelectors *selectors) {
  if (status == 0 || selectors == 0 || !status->detected) {
    return false;
  }

  if (selectors->module != 0) {
    const bool module_match =
        sc_text_equals_ignore_case(status->display_name, selectors->module) ||
        sc_text_equals_ignore_case(status->hello_identity.module_name,
                                   selectors->module);
    if (!module_match) {
      return false;
    }
  }

  if (selectors->uid != 0) {
    if (!sc_text_equals_ignore_case(status->hello_identity.uid,
                                    selectors->uid)) {
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
                                const CliSelectors *selectors, char *error,
                                size_t error_size) {
  if (error != 0 && error_size > 0u) {
    error[0] = '\0';
  }

  if (core == 0 || selectors == 0) {
    if (error != 0 && error_size > 0u) {
      (void)snprintf(error, error_size,
                     "internal error: missing core/selectors");
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
      (void)snprintf(error, error_size,
                     "Ambiguous target: %zu modules detected. Provide "
                     "--module, --uid, or --port.",
                     detected_count);
      return -1;
    }

    matched_index = only_detected_index;
  } else {
    if (matched_count == 0u) {
      (void)snprintf(error, error_size,
                     "No module matches provided selectors.");
      return -1;
    }

    if (matched_count > 1u) {
      (void)snprintf(
          error, error_size,
          "Ambiguous selectors: %zu modules match. Refusing to continue.",
          matched_count);
      return -1;
    }
  }

  if (matched_index < 0) {
    (void)snprintf(error, error_size, "Unable to resolve target module.");
    return -1;
  }

  const ScModuleStatus *target =
      sc_core_module_status(core, (size_t)matched_index);
  if (target == 0) {
    (void)snprintf(error, error_size, "Resolved target index is invalid.");
    return -1;
  }

  if (target->target_ambiguous) {
    (void)snprintf(
        error, error_size,
        "Fail-closed: target '%s' appears on multiple devices (%zu instances).",
        target->display_name, target->detected_instances);
    return -1;
  }

  return matched_index;
}
