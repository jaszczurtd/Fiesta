#include "sc_cli_output.h"

#include <stdio.h>

const char *sc_cli_value_or_dash(const char *value)
{
    return (value != 0 && value[0] != '\0') ? value : "-";
}

void sc_cli_typed_value_to_text(const ScTypedValue *value,
                                char *buffer,
                                size_t buffer_size)
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
            (void)snprintf(buffer, buffer_size, "%s", sc_cli_value_or_dash(value->raw));
            return;
    }
}

void sc_cli_print_module_table(const ScCore *core)
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
            status->detected ? sc_cli_value_or_dash(status->hello_identity.uid) : "-",
            status->detected ? sc_cli_value_or_dash(status->hello_identity.fw_version) : "-",
            status->detected ? sc_cli_value_or_dash(status->hello_identity.build_id) : "-"
        );
    }
}

void sc_cli_print_parsed_values(const ScParamValuesData *values)
{
    if (values == 0) {
        return;
    }

    printf("PARSED count=%zu%s\n", values->count, values->truncated ? " (truncated)" : "");
    for (size_t i = 0u; i < values->count; ++i) {
        char typed[128];
        sc_cli_typed_value_to_text(&values->entries[i].value, typed, sizeof(typed));
        printf(
            "- %s = %s (type=%s)\n",
            values->entries[i].id,
            typed,
            sc_value_type_name(values->entries[i].value.type)
        );
    }
}

void sc_cli_print_parsed_param_list(const ScParamListData *list)
{
    if (list == 0) {
        return;
    }

    printf("PARSED count=%zu%s\n", list->count, list->truncated ? " (truncated)" : "");
    for (size_t i = 0u; i < list->count; ++i) {
        printf("- %s\n", list->ids[i]);
    }
}

void sc_cli_print_parsed_param_detail(const ScParamDetailData *detail)
{
    if (detail == 0) {
        return;
    }

    char value_text[128];
    char min_text[128];
    char max_text[128];
    char default_text[128];
    sc_cli_typed_value_to_text(&detail->value, value_text, sizeof(value_text));
    sc_cli_typed_value_to_text(&detail->min, min_text, sizeof(min_text));
    sc_cli_typed_value_to_text(&detail->max, max_text, sizeof(max_text));
    sc_cli_typed_value_to_text(&detail->default_value, default_text, sizeof(default_text));

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
