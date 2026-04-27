#include "sc_detection.h"

#include <stdio.h>
#include <string.h>

#include "sc_i18n.h"
#include "sc_modules_view.h"

static void detection_result_free(DetectionResult *result)
{
    if (result == 0) {
        return;
    }

    g_free(result->log_text);
    g_free(result);
}

static void append_text(char *dst, size_t dst_size, const char *text)
{
    if (dst == 0 || dst_size == 0u || text == 0 || text[0] == '\0') {
        return;
    }

    const size_t used = strlen(dst);
    if (used >= dst_size - 1u) {
        return;
    }

    (void)snprintf(dst + used, dst_size - used, "%s", text);
}

static bool command_result_is_unknown(const ScCommandResult *result)
{
    if (result == 0) {
        return false;
    }

    if (result->status == SC_COMMAND_STATUS_UNKNOWN_CMD) {
        return true;
    }

    return strcmp(result->response, "ERR UNKNOWN") == 0;
}

#if UI_AUTO_REFRESH_PARAM_PROBE
static const ScParamValueEntry *find_value_entry_by_id(
    const ScParamValuesData *values,
    const char *id
)
{
    if (values == 0 || id == 0) {
        return 0;
    }

    for (size_t i = 0u; i < values->count; ++i) {
        if (strcmp(values->entries[i].id, id) == 0) {
            return &values->entries[i];
        }
    }

    return 0;
}

static bool typed_values_equal(const ScTypedValue *a, const ScTypedValue *b)
{
    if (a == 0 || b == 0) {
        return false;
    }

    if (a->type != b->type) {
        return strcmp(a->raw, b->raw) == 0;
    }

    switch (a->type) {
        case SC_VALUE_TYPE_BOOL:
            return a->bool_value == b->bool_value;
        case SC_VALUE_TYPE_INT:
            return a->int_value == b->int_value;
        case SC_VALUE_TYPE_UINT:
            return a->uint_value == b->uint_value;
        case SC_VALUE_TYPE_FLOAT:
            return a->float_value == b->float_value;
        case SC_VALUE_TYPE_TEXT:
        case SC_VALUE_TYPE_UNKNOWN:
        default:
            return strcmp(a->raw, b->raw) == 0;
    }
}
#endif

static void run_detection_worker(
    GTask *task,
    gpointer source_object,
    gpointer task_data,
    GCancellable *cancellable
)
{
    (void)source_object;
    (void)task_data;
    (void)cancellable;

    DetectionResult *result = g_new0(DetectionResult, 1u);
    result->log_text = g_malloc0(UI_DETECTION_LOG_MAX);

    sc_core_init(&result->core);
    sc_core_detect_modules(&result->core, result->log_text, UI_DETECTION_LOG_MAX);
    append_text(
        result->log_text,
        UI_DETECTION_LOG_MAX,
        sc_i18n_string_get(SC_I18N_LOG_AUTO_REFRESH_HEADER)
    );

    bool has_any_target = false;
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        const ScModuleStatus *status = sc_core_module_status(&result->core, i);
        if (status == 0) {
            const char *unavailable = sc_i18n_string_get(SC_I18N_STATUS_MODULE_UNAVAILABLE);
            (void)snprintf(result->module_meta_status[i],
                           sizeof(result->module_meta_status[i]), "%s", unavailable);
            (void)snprintf(result->module_catalog_status[i],
                           sizeof(result->module_catalog_status[i]), "%s", unavailable);
            (void)snprintf(result->module_values_status[i],
                           sizeof(result->module_values_status[i]), "%s", unavailable);
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]), "%s", unavailable);
            continue;
        }

        if (!status->detected) {
            const char *not_detected = sc_i18n_string_get(SC_I18N_STATUS_NOT_DETECTED);
            (void)snprintf(result->module_meta_status[i],
                           sizeof(result->module_meta_status[i]), "%s", not_detected);
            (void)snprintf(result->module_catalog_status[i],
                           sizeof(result->module_catalog_status[i]), "%s", not_detected);
            (void)snprintf(result->module_values_status[i],
                           sizeof(result->module_values_status[i]), "%s", not_detected);
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]), "%s", not_detected);
            continue;
        }

        if (status->target_ambiguous) {
            (void)snprintf(result->module_meta_status[i],
                           sizeof(result->module_meta_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_AMBIGUOUS_META));
            (void)snprintf(result->module_catalog_status[i],
                           sizeof(result->module_catalog_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_AMBIGUOUS_CATALOG));
            (void)snprintf(result->module_values_status[i],
                           sizeof(result->module_values_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_AMBIGUOUS_VALUES));
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_AMBIGUOUS_PROBE));
            continue;
        }

        has_any_target = true;
        char auto_line[256];
        (void)snprintf(auto_line, sizeof(auto_line),
                       sc_i18n_string_get(SC_I18N_LOG_AUTO_REFRESH_FOR_FMT),
                       status->display_name);
        append_text(result->log_text, UI_DETECTION_LOG_MAX, auto_line);

        ScCommandResult meta_result;
        const bool meta_ok = sc_core_sc_get_meta(
            &result->core,
            i,
            &meta_result,
            result->log_text,
            UI_DETECTION_LOG_MAX
        );
        if (!meta_ok) {
            (void)snprintf(result->module_meta_status[i],
                           sizeof(result->module_meta_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_META_TRANSPORT_ERR));
            const char *skip = sc_i18n_string_get(SC_I18N_STATUS_SKIP_META_TRANSPORT);
            (void)snprintf(result->module_catalog_status[i],
                           sizeof(result->module_catalog_status[i]), "%s", skip);
            (void)snprintf(result->module_values_status[i],
                           sizeof(result->module_values_status[i]), "%s", skip);
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]), "%s", skip);
            continue;
        }

        if (command_result_is_unknown(&meta_result)) {
            (void)snprintf(result->module_meta_status[i],
                           sizeof(result->module_meta_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_PROTOCOL_UNSUPPORTED));
            const char *short_unsupported =
                sc_i18n_string_get(SC_I18N_STATUS_PROTOCOL_UNSUPPORTED_SHORT);
            (void)snprintf(result->module_catalog_status[i],
                           sizeof(result->module_catalog_status[i]),
                           "%s", short_unsupported);
            (void)snprintf(result->module_values_status[i],
                           sizeof(result->module_values_status[i]),
                           "%s", short_unsupported);
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]),
                           "%s", short_unsupported);
            continue;
        }

        if (meta_result.status != SC_COMMAND_STATUS_OK) {
            (void)snprintf(result->module_meta_status[i],
                           sizeof(result->module_meta_status[i]),
                           sc_i18n_string_get(SC_I18N_STATUS_META_FAILED_FMT),
                           sc_command_status_name(meta_result.status));
            const char *skip = sc_i18n_string_get(SC_I18N_STATUS_SKIP_META_FAILED);
            (void)snprintf(result->module_catalog_status[i],
                           sizeof(result->module_catalog_status[i]), "%s", skip);
            (void)snprintf(result->module_values_status[i],
                           sizeof(result->module_values_status[i]), "%s", skip);
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]), "%s", skip);
            continue;
        }

        (void)snprintf(result->module_meta_status[i],
                       sizeof(result->module_meta_status[i]),
                       "%s", sc_i18n_string_get(SC_I18N_STATUS_META_REFRESHED));

        ScCommandResult list_result;
        ScParamListData parsed_list;
        char parse_error[256];
        bool list_ok = sc_core_sc_get_param_list(
            &result->core,
            i,
            &list_result,
            result->log_text,
            UI_DETECTION_LOG_MAX
        );

        if (!list_ok) {
            (void)snprintf(result->module_catalog_status[i],
                           sizeof(result->module_catalog_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_CATALOG_TRANSPORT_ERR));
            const char *skip = sc_i18n_string_get(SC_I18N_STATUS_SKIP_CATALOG_TRANSPORT);
            (void)snprintf(result->module_values_status[i],
                           sizeof(result->module_values_status[i]), "%s", skip);
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]), "%s", skip);
            continue;
        }

        if (!sc_core_parse_param_list_result(&list_result, &parsed_list, parse_error, sizeof(parse_error))) {
            (void)snprintf(result->module_catalog_status[i],
                           sizeof(result->module_catalog_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_CATALOG_PARSE_FAILED));
            const char *skip = sc_i18n_string_get(SC_I18N_STATUS_SKIP_CATALOG_PARSE);
            (void)snprintf(result->module_values_status[i],
                           sizeof(result->module_values_status[i]), "%s", skip);
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]), "%s", skip);
            append_text(result->log_text, UI_DETECTION_LOG_MAX,
                        sc_i18n_string_get(SC_I18N_LOG_WARN_PREFIX));
            append_text(result->log_text, UI_DETECTION_LOG_MAX, parse_error);
            append_text(result->log_text, UI_DETECTION_LOG_MAX, "\n");
            continue;
        }

        (void)snprintf(result->module_catalog_status[i],
                       sizeof(result->module_catalog_status[i]),
                       sc_i18n_string_get(SC_I18N_STATUS_CATALOG_READ_FMT),
                       parsed_list.count,
                       parsed_list.truncated
                           ? sc_i18n_string_get(SC_I18N_STATUS_TRUNCATED_SUFFIX)
                           : "");

        ScCommandResult values_result;
        ScParamValuesData parsed_values;
        bool values_ok = sc_core_sc_get_values(
            &result->core,
            i,
            &values_result,
            result->log_text,
            UI_DETECTION_LOG_MAX
        );
        if (!values_ok) {
            (void)snprintf(result->module_values_status[i],
                           sizeof(result->module_values_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_VALUES_TRANSPORT_ERR));
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_SKIP_VALUES_TRANSPORT));
            continue;
        }

        if (!sc_core_parse_param_values_result(&values_result, &parsed_values, parse_error, sizeof(parse_error))) {
            (void)snprintf(result->module_values_status[i],
                           sizeof(result->module_values_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_VALUES_PARSE_FAILED));
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_SKIP_VALUES_PARSE));
            append_text(result->log_text, UI_DETECTION_LOG_MAX,
                        sc_i18n_string_get(SC_I18N_LOG_WARN_PREFIX));
            append_text(result->log_text, UI_DETECTION_LOG_MAX, parse_error);
            append_text(result->log_text, UI_DETECTION_LOG_MAX, "\n");
            continue;
        }

        (void)snprintf(result->module_values_status[i],
                       sizeof(result->module_values_status[i]),
                       sc_i18n_string_get(SC_I18N_STATUS_VALUES_READ_FMT),
                       parsed_values.count,
                       parsed_values.truncated
                           ? sc_i18n_string_get(SC_I18N_STATUS_TRUNCATED_SUFFIX)
                           : "");

#if UI_AUTO_REFRESH_PARAM_PROBE
        size_t probe_ok_count = 0u;
        size_t probe_fail_count = 0u;
        size_t cross_mismatch_count = 0u;

        for (size_t p = 0u; p < parsed_list.count; ++p) {
            const char *param_id = parsed_list.ids[p];
            ScCommandResult param_result;
            if (!sc_core_sc_get_param(
                    &result->core,
                    i,
                    param_id,
                    &param_result,
                    result->log_text,
                    UI_DETECTION_LOG_MAX
                )) {
                probe_fail_count++;
                continue;
            }

            ScParamDetailData parsed_param;
            if (!sc_core_parse_param_result(&param_result, &parsed_param, parse_error, sizeof(parse_error))) {
                probe_fail_count++;
                continue;
            }

            probe_ok_count++;
            const ScParamValueEntry *snapshot = find_value_entry_by_id(&parsed_values, parsed_param.id);
            if (snapshot != 0 && !typed_values_equal(&snapshot->value, &parsed_param.value)) {
                cross_mismatch_count++;
            }
        }

        if (parsed_list.count == 0u) {
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_NO_IDS_TO_PROBE));
        } else if (probe_fail_count > 0u) {
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]),
                           sc_i18n_string_get(SC_I18N_STATUS_PROBE_PARTIAL_FMT),
                           probe_ok_count,
                           probe_fail_count,
                           cross_mismatch_count);
        } else {
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]),
                           sc_i18n_string_get(SC_I18N_STATUS_PROBE_OK_FMT),
                           probe_ok_count,
                           cross_mismatch_count);
        }
#else
        if (parsed_list.count == 0u) {
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_NO_IDS_TO_PROBE));
        } else {
            (void)snprintf(result->module_param_probe_status[i],
                           sizeof(result->module_param_probe_status[i]),
                           "%s", sc_i18n_string_get(SC_I18N_STATUS_PROBE_SKIPPED));
        }
#endif
    }

    if (!has_any_target) {
        append_text(result->log_text, UI_DETECTION_LOG_MAX,
                    sc_i18n_string_get(SC_I18N_LOG_NO_TARGETS));
    }

    g_task_return_pointer(task, result, (GDestroyNotify)detection_result_free);
}

static void on_detection_finished(GObject *source_object,
                                  GAsyncResult *async_result,
                                  gpointer user_data)
{
    (void)source_object;

    AppState *state = (AppState *)user_data;
    if (state == 0) {
        return;
    }

    DetectionResult *result = g_task_propagate_pointer(G_TASK(async_result), 0);
    state->detection_in_progress = false;

    if (result == 0) {
        if (state->detect_button != 0) {
            gtk_widget_set_sensitive(state->detect_button, TRUE);
            gtk_button_set_label(GTK_BUTTON(state->detect_button),
                                 sc_i18n_string_get(SC_I18N_BTN_DETECT));
        }
        if (state->log_buffer != 0) {
            gtk_text_buffer_set_text(state->log_buffer,
                                     sc_i18n_string_get(SC_I18N_LOG_DETECTION_FAILED),
                                     -1);
        }
        state->connected = false;
        sc_modules_view_refresh_lamps(state);
        sc_modules_view_set_placeholder(state,
            sc_i18n_string_get(SC_I18N_PLACEHOLDER_FAILED));
        sc_modules_view_refresh_details(state);
        return;
    }

    state->core = result->core;
    state->connected = true;
    sc_modules_view_refresh_lamps(state);

    if (state->log_buffer != 0 && result->log_text != 0) {
        gtk_text_buffer_set_text(state->log_buffer, result->log_text, -1);
    }

    if (state->detect_button != 0) {
        gtk_widget_set_sensitive(state->detect_button, TRUE);
        gtk_button_set_label(GTK_BUTTON(state->detect_button),
                             sc_i18n_string_get(SC_I18N_BTN_DISCONNECT));
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        sc_modules_view_set_meta_status(state, i, result->module_meta_status[i]);
        sc_modules_view_set_catalog_status(state, i, result->module_catalog_status[i]);
        sc_modules_view_set_values_status(state, i, result->module_values_status[i]);
        sc_modules_view_set_param_probe_status(state, i, result->module_param_probe_status[i]);
    }

    sc_modules_view_set_placeholder(state,
        sc_i18n_string_get(SC_I18N_PLACEHOLDER_FINISHED));
    sc_modules_view_select_first_detected(state);
    sc_modules_view_refresh_details(state);
    detection_result_free(result);
}

void sc_detection_start_async(AppState *state)
{
    if (state == 0 || state->detection_in_progress) {
        return;
    }

    state->detection_in_progress = true;
    state->connected = false;
    state->selection_valid = false;
    state->selected_module_index = 0u;
    sc_modules_view_set_placeholder(state,
        sc_i18n_string_get(SC_I18N_PLACEHOLDER_DETECTING));
    const char *waiting = sc_i18n_string_get(SC_I18N_STATUS_WAITING);
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        sc_modules_view_set_meta_status(state, i, waiting);
        sc_modules_view_set_catalog_status(state, i, waiting);
        sc_modules_view_set_values_status(state, i, waiting);
        sc_modules_view_set_param_probe_status(state, i, waiting);
    }

    sc_core_reset_detection(&state->core);
    sc_modules_view_refresh_lamps(state);
    sc_modules_view_refresh_details(state);

    if (state->module_list != 0) {
        gtk_list_box_unselect_all(GTK_LIST_BOX(state->module_list));
    }

    if (state->log_buffer != 0) {
        gtk_text_buffer_set_text(state->log_buffer,
                                 sc_i18n_string_get(SC_I18N_LOG_DETECTING),
                                 -1);
    }

    if (state->detect_button != 0) {
        gtk_button_set_label(GTK_BUTTON(state->detect_button),
                             sc_i18n_string_get(SC_I18N_BTN_DETECTING));
        gtk_widget_set_sensitive(state->detect_button, FALSE);
    }

    GTask *task = g_task_new(0, 0, on_detection_finished, state);
    g_task_run_in_thread(task, run_detection_worker);
    g_object_unref(task);
}

void sc_detection_reset_connection(AppState *state, bool by_user_request)
{
    if (state == 0) {
        return;
    }

    sc_core_reset_detection(&state->core);
    sc_modules_view_refresh_lamps(state);

    state->connected = false;
    state->detection_in_progress = false;
    state->selection_valid = false;
    state->selected_module_index = 0u;
    sc_modules_view_set_placeholder(state,
        sc_i18n_string_get(SC_I18N_PLACEHOLDER_INITIAL));
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        sc_modules_view_set_meta_status(state, i, sc_i18n_string_get(SC_I18N_STATUS_NO_META));
        sc_modules_view_set_catalog_status(state, i, sc_i18n_string_get(SC_I18N_STATUS_NO_CATALOG));
        sc_modules_view_set_values_status(state, i, sc_i18n_string_get(SC_I18N_STATUS_NO_VALUES));
        sc_modules_view_set_param_probe_status(state, i, sc_i18n_string_get(SC_I18N_STATUS_NO_PROBE));
    }

    if (state->module_list != 0) {
        gtk_list_box_unselect_all(GTK_LIST_BOX(state->module_list));
    }

    if (state->detect_button != 0) {
        gtk_widget_set_sensitive(state->detect_button, TRUE);
        gtk_button_set_label(GTK_BUTTON(state->detect_button),
                             sc_i18n_string_get(SC_I18N_BTN_DETECT));
    }

    if (state->log_buffer != 0) {
        gtk_text_buffer_set_text(
            state->log_buffer,
            sc_i18n_string_get(by_user_request
                        ? SC_I18N_LOG_DISCONNECTED
                        : SC_I18N_LOG_IDLE),
            -1
        );
    }

    sc_modules_view_refresh_details(state);
}

void sc_detection_on_detect_clicked(GtkButton *button, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    if (state == 0 || state->log_buffer == 0) {
        return;
    }

    if (state->connected) {
        sc_detection_reset_connection(state, true);
        return;
    }

    (void)button;
    sc_detection_start_async(state);
}
