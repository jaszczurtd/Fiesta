#include "sc_modules_view.h"

#include <stdio.h>

#include "sc_flash_tab.h"
#include "sc_generic_gfx_helper.h"
#include "sc_i18n.h"

static int module_index_from_row(const AppState *state,
                                 const GtkListBoxRow *row)
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

void sc_modules_view_set_meta_status(AppState *state,
                                     size_t module_index,
                                     const char *message)
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

void sc_modules_view_set_catalog_status(AppState *state,
                                        size_t module_index,
                                        const char *message)
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

void sc_modules_view_set_values_status(AppState *state,
                                       size_t module_index,
                                       const char *message)
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

void sc_modules_view_set_param_probe_status(AppState *state,
                                            size_t module_index,
                                            const char *message)
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

void sc_modules_view_set_placeholder(AppState *state, const char *message)
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

void sc_modules_view_refresh_details(AppState *state)
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

void sc_modules_view_refresh_lamps(AppState *state)
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

        sc_gfx_set_lamp_state(state->module_lamps[i], status->detected);

        if (state->module_name_labels[i] != 0) {
            char label_text[96];
            if (!status->detected) {
                (void)snprintf(label_text, sizeof(label_text), "%s", status->display_name);
            } else if (status->target_ambiguous) {
                /* The format key holds the full suffix template
                 * (e.g. " (x%zu, ambiguous)"); the display name is
                 * the immutable module identity from core, never
                 * localized. */
                char suffix[64];
                (void)snprintf(suffix, sizeof(suffix),
                               sc_i18n_string_get(SC_I18N_LABEL_AMBIGUOUS_FMT),
                               status->detected_instances);
                (void)snprintf(label_text, sizeof(label_text),
                               "%s%s", status->display_name, suffix);
            } else {
                (void)snprintf(label_text, sizeof(label_text),
                               "%s%s",
                               status->display_name,
                               sc_i18n_string_get(SC_I18N_LABEL_DETECTED_SUFFIX));
            }

            gtk_label_set_text(GTK_LABEL(state->module_name_labels[i]), label_text);
        }
    }

    /* Detection state changed -> downstream subviews re-evaluate
     * their own gating. The Flash tab also rebuilds its per-module
     * sections to reflect which modules are now in-scope (Phase 6.2). */
    sc_flash_tab_refresh_sensitivity(state);
    sc_flash_tab_rebuild_sections(state);
}

void sc_modules_view_select_first_detected(AppState *state)
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

void sc_modules_view_on_row_selected(GtkListBox *box,
                                     GtkListBoxRow *row,
                                     gpointer user_data)
{
    (void)box;

    AppState *state = (AppState *)user_data;
    if (state == 0) {
        return;
    }

    if (row == 0) {
        state->selection_valid = false;
        state->selected_module_index = 0u;
        sc_modules_view_set_placeholder(state,
            sc_i18n_string_get(SC_I18N_PLACEHOLDER_SELECT_MODULE));
        sc_modules_view_refresh_details(state);
        return;
    }

    const int index = module_index_from_row(state, row);
    if (index < 0) {
        state->selection_valid = false;
        state->selected_module_index = 0u;
        sc_modules_view_set_placeholder(state,
            sc_i18n_string_get(SC_I18N_PLACEHOLDER_UNKNOWN_ROW));
        sc_modules_view_refresh_details(state);
        return;
    }

    state->selection_valid = true;
    state->selected_module_index = (size_t)index;
    sc_modules_view_refresh_details(state);
}

void sc_modules_view_build(AppState *state, GtkWidget *parent_box)
{
    if (state == 0 || parent_box == 0) {
        return;
    }

    /* Top row: Detect button on the left, spacer expands. The button
     * itself is wired to its click handler by the orchestrator that
     * owns the detection state - see sc_app.c. */
    GtkWidget *top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(parent_box), top_row);

    GtkWidget *detect_button = gtk_button_new_with_label(
        sc_i18n_string_get(SC_I18N_BTN_DETECT)
    );
    gtk_box_append(GTK_BOX(top_row), detect_button);
    state->detect_button = detect_button;

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(top_row), spacer);

    /* Main row: module list on the left, details panel on the right. */
    GtkWidget *main_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(parent_box), main_row);

    GtkWidget *modules_frame = gtk_frame_new(sc_i18n_string_get(SC_I18N_FRAME_MODULES));
    gtk_widget_set_size_request(modules_frame, 300, -1);
    gtk_box_append(GTK_BOX(main_row), modules_frame);

    GtkWidget *module_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(module_list), GTK_SELECTION_SINGLE);
    gtk_frame_set_child(GTK_FRAME(modules_frame), module_list);
    state->module_list = module_list;
    g_signal_connect(module_list,
                     "row-selected",
                     G_CALLBACK(sc_modules_view_on_row_selected),
                     state);

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
        sc_gfx_set_lamp_state(lamp, false);
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

    /* Bottom: log view scrolled. */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC
    );
    gtk_box_append(GTK_BOX(parent_box), scrolled);

    GtkWidget *log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), log_view);

    state->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
}
