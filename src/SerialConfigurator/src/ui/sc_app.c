#include <gtk/gtk.h>

#include <stdio.h>
#include <string.h>

#include "sc_app.h"
#include "sc_core.h"
#include "sc_module_details.h"

typedef struct AppState {
    ScCore core;
    GtkTextBuffer *log_buffer;
    GtkWidget *detect_button;
    GtkWidget *module_list;
    GtkWidget *module_rows[SC_MODULE_COUNT];
    GtkWidget *module_lamps[SC_MODULE_COUNT];
    GtkWidget *module_name_labels[SC_MODULE_COUNT];
    ScModuleDetailsView details_view;
    bool connected;
    bool detection_in_progress;
    bool selection_valid;
    size_t selected_module_index;
    char placeholder_status[160];
    char module_meta_status[SC_MODULE_COUNT][160];
    char module_catalog_status[SC_MODULE_COUNT][160];
    char module_values_status[SC_MODULE_COUNT][160];
    char module_param_probe_status[SC_MODULE_COUNT][160];
} AppState;

typedef struct DetectionResult {
    ScCore core;
    char module_meta_status[SC_MODULE_COUNT][160];
    char module_catalog_status[SC_MODULE_COUNT][160];
    char module_values_status[SC_MODULE_COUNT][160];
    char module_param_probe_status[SC_MODULE_COUNT][160];
    char *log_text;
} DetectionResult;

#define UI_DETECTION_LOG_MAX 16384u
#define UI_AUTO_REFRESH_PARAM_PROBE 0

static const char *k_detect_label = "Detect Fiesta Modules";
static const char *k_disconnect_label = "Disconnect";
static const char *k_detecting_label = "Detecting...";
static const char *k_idle_log_message =
    "Press \"Detect Fiesta Modules\" to send HELLO to connected modules.\n";

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

static void set_module_meta_status(AppState *state, size_t module_index, const char *message)
{
    if (state == 0 || module_index >= SC_MODULE_COUNT) {
        return;
    }

    (void)snprintf(
        state->module_meta_status[module_index],
        sizeof(state->module_meta_status[module_index]),
        "%s",
        (message != 0 && message[0] != '\0') ? message : "-"
    );
}

static void set_module_catalog_status(AppState *state, size_t module_index, const char *message)
{
    if (state == 0 || module_index >= SC_MODULE_COUNT) {
        return;
    }

    (void)snprintf(
        state->module_catalog_status[module_index],
        sizeof(state->module_catalog_status[module_index]),
        "%s",
        (message != 0 && message[0] != '\0') ? message : "-"
    );
}

static void set_module_values_status(AppState *state, size_t module_index, const char *message)
{
    if (state == 0 || module_index >= SC_MODULE_COUNT) {
        return;
    }

    (void)snprintf(
        state->module_values_status[module_index],
        sizeof(state->module_values_status[module_index]),
        "%s",
        (message != 0 && message[0] != '\0') ? message : "-"
    );
}

static void set_module_param_probe_status(AppState *state, size_t module_index, const char *message)
{
    if (state == 0 || module_index >= SC_MODULE_COUNT) {
        return;
    }

    (void)snprintf(
        state->module_param_probe_status[module_index],
        sizeof(state->module_param_probe_status[module_index]),
        "%s",
        (message != 0 && message[0] != '\0') ? message : "-"
    );
}

static void set_placeholder_status(AppState *state, const char *message)
{
    if (state == 0) {
        return;
    }

    (void)snprintf(
        state->placeholder_status,
        sizeof(state->placeholder_status),
        "%s",
        (message != 0 && message[0] != '\0') ? message : "-"
    );
}

static void set_lamp_state(GtkWidget *lamp, bool detected)
{
    if (lamp == 0) {
        return;
    }

    gtk_widget_remove_css_class(lamp, "status-red");
    gtk_widget_remove_css_class(lamp, "status-green");
    gtk_widget_add_css_class(lamp, detected ? "status-green" : "status-red");
}

static int module_index_from_row(const AppState *state, const GtkListBoxRow *row)
{
    if (state == 0 || row == 0) {
        return -1;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        if (state->module_rows[i] == GTK_WIDGET(row)) {
            return (int)i;
        }
    }

    return -1;
}

static void refresh_details_view(AppState *state)
{
    if (state == 0) {
        return;
    }

    if (!state->selection_valid) {
        sc_module_details_show_placeholder(&state->details_view, state->placeholder_status);
        return;
    }

    const ScModuleStatus *status = sc_core_module_status(&state->core, state->selected_module_index);
    if (status == 0) {
        sc_module_details_show_placeholder(&state->details_view, state->placeholder_status);
        return;
    }

    sc_module_details_show_module(
        &state->details_view,
        status,
        state->module_meta_status[state->selected_module_index],
        state->module_catalog_status[state->selected_module_index],
        state->module_values_status[state->selected_module_index],
        state->module_param_probe_status[state->selected_module_index]
    );
}

static void refresh_module_lamps(AppState *state)
{
    if (state == 0) {
        return;
    }

    const size_t module_count = sc_core_module_count();
    for (size_t i = 0u; i < module_count; ++i) {
        const ScModuleStatus *status = sc_core_module_status(&state->core, i);
        if (status == 0) {
            continue;
        }

        set_lamp_state(state->module_lamps[i], status->detected);

        if (state->module_name_labels[i] != 0) {
            char label_text[96];
            if (!status->detected) {
                (void)snprintf(label_text, sizeof(label_text), "%s", status->display_name);
            } else if (status->target_ambiguous) {
                (void)snprintf(
                    label_text,
                    sizeof(label_text),
                    "%s (x%zu, ambiguous)",
                    status->display_name,
                    status->detected_instances
                );
            } else {
                (void)snprintf(label_text, sizeof(label_text), "%s (detected)", status->display_name);
            }

            gtk_label_set_text(GTK_LABEL(state->module_name_labels[i]), label_text);
        }
    }
}

static void select_first_detected_module(AppState *state)
{
    if (state == 0 || state->module_list == 0) {
        return;
    }

    for (size_t i = 0u; i < sc_core_module_count(); ++i) {
        const ScModuleStatus *status = sc_core_module_status(&state->core, i);
        if (status != 0 && status->detected && state->module_rows[i] != 0) {
            gtk_list_box_select_row(
                GTK_LIST_BOX(state->module_list),
                GTK_LIST_BOX_ROW(state->module_rows[i])
            );
            return;
        }
    }

    gtk_list_box_unselect_all(GTK_LIST_BOX(state->module_list));
}

static void reset_connection_state(AppState *state, bool by_user_request)
{
    if (state == 0) {
        return;
    }

    sc_core_reset_detection(&state->core);
    refresh_module_lamps(state);

    state->connected = false;
    state->detection_in_progress = false;
    state->selection_valid = false;
    state->selected_module_index = 0u;
    set_placeholder_status(state, "Run detection to populate module details.");
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        set_module_meta_status(state, i, "No metadata (module not detected).");
        set_module_catalog_status(state, i, "No catalog read (module not detected).");
        set_module_values_status(state, i, "No values read (module not detected).");
        set_module_param_probe_status(state, i, "No param probe (module not detected).");
    }

    if (state->module_list != 0) {
        gtk_list_box_unselect_all(GTK_LIST_BOX(state->module_list));
    }

    if (state->detect_button != 0) {
        gtk_widget_set_sensitive(state->detect_button, TRUE);
        gtk_button_set_label(GTK_BUTTON(state->detect_button), k_detect_label);
    }

    if (state->log_buffer != 0) {
        if (by_user_request) {
            gtk_text_buffer_set_text(
                state->log_buffer,
                "Disconnected. Application state has been reset.\n"
                "Press \"Detect Fiesta Modules\" to start detection again.\n",
                -1
            );
        } else {
            gtk_text_buffer_set_text(state->log_buffer, k_idle_log_message, -1);
        }
    }

    refresh_details_view(state);
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
        "\n[INFO] Starting automatic metadata refresh for detected modules...\n"
    );

    bool has_any_target = false;
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        const ScModuleStatus *status = sc_core_module_status(&result->core, i);
        if (status == 0) {
            (void)snprintf(
                result->module_meta_status[i],
                sizeof(result->module_meta_status[i]),
                "Module status unavailable."
            );
            (void)snprintf(
                result->module_catalog_status[i],
                sizeof(result->module_catalog_status[i]),
                "Module status unavailable."
            );
            (void)snprintf(
                result->module_values_status[i],
                sizeof(result->module_values_status[i]),
                "Module status unavailable."
            );
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "Module status unavailable."
            );
            continue;
        }

        if (!status->detected) {
            (void)snprintf(
                result->module_meta_status[i],
                sizeof(result->module_meta_status[i]),
                "Module not detected."
            );
            (void)snprintf(
                result->module_catalog_status[i],
                sizeof(result->module_catalog_status[i]),
                "Module not detected."
            );
            (void)snprintf(
                result->module_values_status[i],
                sizeof(result->module_values_status[i]),
                "Module not detected."
            );
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "Module not detected."
            );
            continue;
        }

        if (status->target_ambiguous) {
            (void)snprintf(
                result->module_meta_status[i],
                sizeof(result->module_meta_status[i]),
                "Ambiguous target: metadata skipped."
            );
            (void)snprintf(
                result->module_catalog_status[i],
                sizeof(result->module_catalog_status[i]),
                "Ambiguous target: catalog skipped."
            );
            (void)snprintf(
                result->module_values_status[i],
                sizeof(result->module_values_status[i]),
                "Ambiguous target: values skipped."
            );
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "Ambiguous target: param probes skipped."
            );
            continue;
        }

        has_any_target = true;
        append_text(result->log_text, UI_DETECTION_LOG_MAX, "\n[INFO] Automatic metadata refresh for ");
        append_text(result->log_text, UI_DETECTION_LOG_MAX, status->display_name);
        append_text(result->log_text, UI_DETECTION_LOG_MAX, "...\n");

        ScCommandResult meta_result;
        const bool meta_ok = sc_core_sc_get_meta(
            &result->core,
            i,
            &meta_result,
            result->log_text,
            UI_DETECTION_LOG_MAX
        );
        if (!meta_ok) {
            (void)snprintf(
                result->module_meta_status[i],
                sizeof(result->module_meta_status[i]),
                "SC_GET_META transport error."
            );
            (void)snprintf(
                result->module_catalog_status[i],
                sizeof(result->module_catalog_status[i]),
                "Skipped: metadata transport failed."
            );
            (void)snprintf(
                result->module_values_status[i],
                sizeof(result->module_values_status[i]),
                "Skipped: metadata transport failed."
            );
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "Skipped: metadata transport failed."
            );
            continue;
        }

        if (command_result_is_unknown(&meta_result)) {
            (void)snprintf(
                result->module_meta_status[i],
                sizeof(result->module_meta_status[i]),
                "SC protocol not supported by this firmware yet."
            );
            (void)snprintf(
                result->module_catalog_status[i],
                sizeof(result->module_catalog_status[i]),
                "SC protocol not supported."
            );
            (void)snprintf(
                result->module_values_status[i],
                sizeof(result->module_values_status[i]),
                "SC protocol not supported."
            );
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "SC protocol not supported."
            );
            continue;
        }

        if (meta_result.status != SC_COMMAND_STATUS_OK) {
            (void)snprintf(
                result->module_meta_status[i],
                sizeof(result->module_meta_status[i]),
                "SC_GET_META failed: %s",
                sc_command_status_name(meta_result.status)
            );
            (void)snprintf(
                result->module_catalog_status[i],
                sizeof(result->module_catalog_status[i]),
                "Skipped: metadata failed."
            );
            (void)snprintf(
                result->module_values_status[i],
                sizeof(result->module_values_status[i]),
                "Skipped: metadata failed."
            );
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "Skipped: metadata failed."
            );
            continue;
        }

        (void)snprintf(
            result->module_meta_status[i],
            sizeof(result->module_meta_status[i]),
            "Metadata refreshed."
        );

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
            (void)snprintf(
                result->module_catalog_status[i],
                sizeof(result->module_catalog_status[i]),
                "SC_GET_PARAM_LIST transport error."
            );
            (void)snprintf(
                result->module_values_status[i],
                sizeof(result->module_values_status[i]),
                "Skipped: catalog transport failed."
            );
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "Skipped: catalog transport failed."
            );
            continue;
        }

        if (!sc_core_parse_param_list_result(&list_result, &parsed_list, parse_error, sizeof(parse_error))) {
            (void)snprintf(
                result->module_catalog_status[i],
                sizeof(result->module_catalog_status[i]),
                "Catalog parse failed."
            );
            (void)snprintf(
                result->module_values_status[i],
                sizeof(result->module_values_status[i]),
                "Skipped: catalog parse failed."
            );
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "Skipped: catalog parse failed."
            );
            append_text(result->log_text, UI_DETECTION_LOG_MAX, "[WARN] ");
            append_text(result->log_text, UI_DETECTION_LOG_MAX, parse_error);
            append_text(result->log_text, UI_DETECTION_LOG_MAX, "\n");
            continue;
        }

        (void)snprintf(
            result->module_catalog_status[i],
            sizeof(result->module_catalog_status[i]),
            "Catalog read: %zu id(s)%s",
            parsed_list.count,
            parsed_list.truncated ? " (truncated)" : ""
        );

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
            (void)snprintf(
                result->module_values_status[i],
                sizeof(result->module_values_status[i]),
                "SC_GET_VALUES transport error."
            );
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "Skipped: values transport failed."
            );
            continue;
        }

        if (!sc_core_parse_param_values_result(&values_result, &parsed_values, parse_error, sizeof(parse_error))) {
            (void)snprintf(
                result->module_values_status[i],
                sizeof(result->module_values_status[i]),
                "Values parse failed."
            );
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "Skipped: values parse failed."
            );
            append_text(result->log_text, UI_DETECTION_LOG_MAX, "[WARN] ");
            append_text(result->log_text, UI_DETECTION_LOG_MAX, parse_error);
            append_text(result->log_text, UI_DETECTION_LOG_MAX, "\n");
            continue;
        }

        (void)snprintf(
            result->module_values_status[i],
            sizeof(result->module_values_status[i]),
            "Values read: %zu entry(ies)%s",
            parsed_values.count,
            parsed_values.truncated ? " (truncated)" : ""
        );

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
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "No parameter ids to probe."
            );
        } else if (probe_fail_count > 0u) {
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "Probe partial: ok=%zu fail=%zu mismatch=%zu",
                probe_ok_count,
                probe_fail_count,
                cross_mismatch_count
            );
        } else {
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "Probe ok: %zu id(s), mismatch=%zu",
                probe_ok_count,
                cross_mismatch_count
            );
        }
#else
        if (parsed_list.count == 0u) {
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "No parameter ids to probe."
            );
        } else {
            (void)snprintf(
                result->module_param_probe_status[i],
                sizeof(result->module_param_probe_status[i]),
                "Probe skipped (fast auto-refresh)."
            );
        }
#endif
    }

    if (!has_any_target) {
        append_text(
            result->log_text,
            UI_DETECTION_LOG_MAX,
            "[INFO] No detected module eligible for metadata refresh.\n"
        );
    }

    g_task_return_pointer(task, result, (GDestroyNotify)detection_result_free);
}

static void on_detection_finished(GObject *source_object, GAsyncResult *async_result, gpointer user_data)
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
            gtk_button_set_label(GTK_BUTTON(state->detect_button), k_detect_label);
        }
        if (state->log_buffer != 0) {
            gtk_text_buffer_set_text(
                state->log_buffer,
                "Detection failed: internal task error.\n",
                -1
            );
        }
        state->connected = false;
        refresh_module_lamps(state);
        set_placeholder_status(state, "Detection/metadata workflow failed.");
        refresh_details_view(state);
        return;
    }

    state->core = result->core;
    state->connected = true;
    refresh_module_lamps(state);

    if (state->log_buffer != 0 && result->log_text != 0) {
        gtk_text_buffer_set_text(state->log_buffer, result->log_text, -1);
    }

    if (state->detect_button != 0) {
        gtk_widget_set_sensitive(state->detect_button, TRUE);
        gtk_button_set_label(GTK_BUTTON(state->detect_button), k_disconnect_label);
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        set_module_meta_status(state, i, result->module_meta_status[i]);
        set_module_catalog_status(state, i, result->module_catalog_status[i]);
        set_module_values_status(state, i, result->module_values_status[i]);
        set_module_param_probe_status(state, i, result->module_param_probe_status[i]);
    }

    set_placeholder_status(state, "Detection + metadata refresh finished.");
    select_first_detected_module(state);
    refresh_details_view(state);
    detection_result_free(result);
}

static void start_detection_async(AppState *state)
{
    if (state == 0 || state->detection_in_progress) {
        return;
    }

    state->detection_in_progress = true;
    state->connected = false;
    state->selection_valid = false;
    state->selected_module_index = 0u;
    set_placeholder_status(state, "Detecting modules...");
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        set_module_meta_status(state, i, "Waiting for detection...");
        set_module_catalog_status(state, i, "Waiting for detection...");
        set_module_values_status(state, i, "Waiting for detection...");
        set_module_param_probe_status(state, i, "Waiting for detection...");
    }

    sc_core_reset_detection(&state->core);
    refresh_module_lamps(state);
    refresh_details_view(state);

    if (state->module_list != 0) {
        gtk_list_box_unselect_all(GTK_LIST_BOX(state->module_list));
    }

    if (state->log_buffer != 0) {
        gtk_text_buffer_set_text(
            state->log_buffer,
            "Detection started... please wait.\n",
            -1
        );
    }

    if (state->detect_button != 0) {
        gtk_button_set_label(GTK_BUTTON(state->detect_button), k_detecting_label);
        gtk_widget_set_sensitive(state->detect_button, FALSE);
    }

    GTask *task = g_task_new(0, 0, on_detection_finished, state);
    g_task_run_in_thread(task, run_detection_worker);
    g_object_unref(task);
}

static void on_detect_clicked(GtkButton *button, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    if (state == 0 || state->log_buffer == 0) {
        return;
    }

    if (state->connected) {
        reset_connection_state(state, true);
        return;
    }

    (void)button;
    start_detection_async(state);
}

static void on_module_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    (void)box;

    AppState *state = (AppState *)user_data;
    if (state == 0) {
        return;
    }

    if (row == 0) {
        state->selection_valid = false;
        state->selected_module_index = 0u;
        set_placeholder_status(state, "Select a module to view details.");
        refresh_details_view(state);
        return;
    }

    const int index = module_index_from_row(state, row);
    if (index < 0) {
        state->selection_valid = false;
        state->selected_module_index = 0u;
        set_placeholder_status(state, "Unknown module row selected.");
        refresh_details_view(state);
        return;
    }

    state->selection_valid = true;
    state->selected_module_index = (size_t)index;
    refresh_details_view(state);
}

static void install_css(void)
{
    static const char *css =
        ".status-lamp {"
        "  min-width: 14px;"
        "  min-height: 14px;"
        "  border-radius: 7px;"
        "}"
        ".status-red {"
        "  background-color: #d32f2f;"
        "}"
        ".status-green {"
        "  background-color: #2e7d32;"
        "}"
        ".dim-label {"
        "  color: #5f6368;"
        "}";

    GtkCssProvider *provider = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, css);
#else
    gtk_css_provider_load_from_data(provider, css, -1);
#endif
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

static void on_activate(GtkApplication *app, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    if (state == 0) {
        return;
    }

    install_css();

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Serial Configurator");
    gtk_window_set_default_size(GTK_WINDOW(window), 1040, 680);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(root, 16);
    gtk_widget_set_margin_end(root, 16);
    gtk_widget_set_margin_top(root, 16);
    gtk_widget_set_margin_bottom(root, 16);
    gtk_window_set_child(GTK_WINDOW(window), root);

    GtkWidget *top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(root), top_row);

    GtkWidget *detect_button = gtk_button_new_with_label(k_detect_label);
    gtk_box_append(GTK_BOX(top_row), detect_button);
    state->detect_button = detect_button;
    g_signal_connect(detect_button, "clicked", G_CALLBACK(on_detect_clicked), state);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(top_row), spacer);

    GtkWidget *main_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(root), main_row);

    GtkWidget *modules_frame = gtk_frame_new("Modules");
    gtk_widget_set_size_request(modules_frame, 300, -1);
    gtk_box_append(GTK_BOX(main_row), modules_frame);

    GtkWidget *module_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(module_list), GTK_SELECTION_SINGLE);
    gtk_frame_set_child(GTK_FRAME(modules_frame), module_list);
    state->module_list = module_list;
    g_signal_connect(module_list, "row-selected", G_CALLBACK(on_module_row_selected), state);

    const size_t module_count = sc_core_module_count();
    for (size_t i = 0u; i < module_count; ++i) {
        const ScModuleStatus *status = sc_core_module_status(&state->core, i);
        if (status == 0) {
            continue;
        }

        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row_box, 10);
        gtk_widget_set_margin_end(row_box, 10);
        gtk_widget_set_margin_top(row_box, 8);
        gtk_widget_set_margin_bottom(row_box, 8);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        gtk_list_box_append(GTK_LIST_BOX(module_list), row);
        state->module_rows[i] = row;

        GtkWidget *lamp = gtk_label_new("");
        gtk_widget_set_size_request(lamp, 14, 14);
        gtk_widget_add_css_class(lamp, "status-lamp");
        set_lamp_state(lamp, false);
        state->module_lamps[i] = lamp;

        GtkWidget *name_label = gtk_label_new(status->display_name);
        gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
        state->module_name_labels[i] = name_label;

        gtk_box_append(GTK_BOX(row_box), lamp);
        gtk_box_append(GTK_BOX(row_box), name_label);
    }

    sc_module_details_init(&state->details_view);
    GtkWidget *details_root = sc_module_details_root(&state->details_view);
    gtk_widget_set_hexpand(details_root, TRUE);
    gtk_box_append(GTK_BOX(main_row), details_root);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC
    );
    gtk_box_append(GTK_BOX(root), scrolled);

    GtkWidget *log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), log_view);

    state->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    reset_connection_state(state, false);

    gtk_window_present(GTK_WINDOW(window));
}

int sc_app_run(int argc, char *argv[])
{
    AppState state;
    sc_core_init(&state.core);
    state.log_buffer = 0;
    state.detect_button = 0;
    state.module_list = 0;
    state.connected = false;
    state.detection_in_progress = false;
    state.selection_valid = false;
    state.selected_module_index = 0u;
    (void)snprintf(
        state.placeholder_status,
        sizeof(state.placeholder_status),
        "%s",
        "Run detection to populate module details."
    );

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        state.module_rows[i] = 0;
        state.module_lamps[i] = 0;
        state.module_name_labels[i] = 0;
        (void)snprintf(
            state.module_meta_status[i],
            sizeof(state.module_meta_status[i]),
            "%s",
            "No metadata (module not detected)."
        );
        (void)snprintf(
            state.module_catalog_status[i],
            sizeof(state.module_catalog_status[i]),
            "%s",
            "No catalog read (module not detected)."
        );
        (void)snprintf(
            state.module_values_status[i],
            sizeof(state.module_values_status[i]),
            "%s",
            "No values read (module not detected)."
        );
        (void)snprintf(
            state.module_param_probe_status[i],
            sizeof(state.module_param_probe_status[i]),
            "%s",
            "No param probe (module not detected)."
        );
    }

    GtkApplication *app = gtk_application_new(
        "pl.jaszczur.fiesta.serialconfigurator",
        (GApplicationFlags)0
    );

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &state);

    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
