#ifndef SC_CLI_OUTPUT_H
#define SC_CLI_OUTPUT_H

/*
 * stdout / stderr formatting helpers for the CLI. Pure printers - no
 * detection, no transport, no manifest. Every function takes already-
 * resolved data structs from sc_core (parsed identity, parsed param
 * lists / values / details) and renders them to stdout in a
 * deterministic columnar layout suitable for shell piping.
 *
 * Strings here are intentionally English. CLI output is a developer /
 * bench-operator surface; protocol tokens, log prefixes, and column
 * headers stay verbatim across locales (see provider §4.7 Rule 2 -
 * the i18n layer is GUI-only by design).
 */

#include <stddef.h>

#include "sc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Return @p value when non-empty, otherwise the dash filler. */
const char *sc_cli_value_or_dash(const char *value);

/**
 * @brief Render a parsed typed value into @p buffer. Produces
 *        `true`/`false` for bools, decimal for integers, `%.6g` for
 *        floats, raw text otherwise. Always writes a NUL terminator.
 */
void sc_cli_typed_value_to_text(const ScTypedValue *value,
                                char *buffer,
                                size_t buffer_size);

/** @brief Print the module-detection table (used by `list`). */
void sc_cli_print_module_table(const ScCore *core);

/** @brief Print parsed `SC_GET_VALUES` output (used by `get-values`). */
void sc_cli_print_parsed_values(const ScParamValuesData *values);

/** @brief Print parsed `SC_GET_PARAM_LIST` output (used by `param-list`). */
void sc_cli_print_parsed_param_list(const ScParamListData *list);

/** @brief Print parsed `SC_GET_PARAM <id>` detail (used by `get-param`). */
void sc_cli_print_parsed_param_detail(const ScParamDetailData *detail);

#ifdef __cplusplus
}
#endif

#endif /* SC_CLI_OUTPUT_H */
