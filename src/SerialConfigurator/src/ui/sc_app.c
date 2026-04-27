/*
 * Top-level GTK application entry. Owns the GtkApplication object,
 * the AppState lifetime, and the on_activate orchestration that
 * stitches together the modules-view (notebook page 1), the flash
 * tab (notebook page 2), and the detection orchestrator.
 *
 * Anything that has its own widget tree, status state, or signal
 * handlers lives in a dedicated subview file:
 *
 *   sc_modules_view.{h,c}       — page 1: list, lamps, details
 *   sc_flash_tab.{h,c}          — page 2: Flash tab body + gating
 *   sc_detection.{h,c}          — async worker + Detect/Disconnect
 *   sc_generic_gfx_helper.{h,c} — install_css, set_lamp_state
 *   sc_module_details.{h,c}     — right-hand module details panel
 *
 * The shared AppState struct lives in sc_ui_state.h and is treated
 * as a private cross-subview header. All subview files include it.
 */

#include <gtk/gtk.h>

#include <stdio.h>
#include <string.h>

#include "sc_app.h"
#include "sc_core.h"
#include "sc_detection.h"
#include "sc_flash_paths.h"
#include "sc_flash_tab.h"
#include "sc_generic_gfx_helper.h"
#include "sc_i18n.h"
#include "sc_modules_view.h"
#include "sc_ui_state.h"

static void on_activate(GtkApplication *app, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    if (state == 0) {
        return;
    }

    sc_gfx_install_css();

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), sc_i18n_string_get(SC_I18N_APP_TITLE));
    gtk_window_set_default_size(GTK_WINDOW(window), 1040, 680);

    /* Phase 6.1: notebook with two pages — legacy modules view first,
     * empty flash tab second. Subsequent slices flesh out page 2. */
    GtkWidget *notebook = gtk_notebook_new();
    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_window_set_child(GTK_WINDOW(window), notebook);

    /* Page 1 — Modules. */
    GtkWidget *modules_root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(modules_root, 16);
    gtk_widget_set_margin_end(modules_root, 16);
    gtk_widget_set_margin_top(modules_root, 16);
    gtk_widget_set_margin_bottom(modules_root, 16);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             modules_root,
                             gtk_label_new(sc_i18n_string_get(SC_I18N_TAB_MODULES)));

    sc_modules_view_build(state, modules_root);

    /* The Detect button comes from sc_modules_view but its click
     * handler belongs to sc_detection. Wire them here, where both
     * subviews are in scope. */
    if (state->detect_button != 0) {
        g_signal_connect(state->detect_button,
                         "clicked",
                         G_CALLBACK(sc_detection_on_detect_clicked),
                         state);
    }

    /* Page 2 — Flash. Built before reset_connection so that
     * flash_tab_root is set when refresh_module_lamps fires its
     * downstream sensitivity refresh. */
    GtkWidget *flash_root = sc_flash_tab_build(state);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             flash_root,
                             gtk_label_new(sc_i18n_string_get(SC_I18N_TAB_FLASH)));

    /* Idle initial state — modules not detected, log shows the
     * "press Detect" prompt, flash tab grey via the lamp refresh. */
    sc_detection_reset_connection(state, false);

    gtk_window_present(GTK_WINDOW(window));
}

int sc_app_run(int argc, char *argv[])
{
    AppState state;
    sc_core_init(&state.core);
    state.log_buffer = 0;
    state.detect_button = 0;
    state.module_list = 0;
    state.flash_tab_root = 0;
    sc_flash_paths_init(&state.flash_paths);
    state.flash_in_progress = false;
    state.connected = false;
    state.detection_in_progress = false;
    state.selection_valid = false;
    state.selected_module_index = 0u;
    (void)snprintf(
        state.placeholder_status,
        sizeof(state.placeholder_status),
        "%s",
        sc_i18n_string_get(SC_I18N_PLACEHOLDER_INITIAL)
    );

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        state.module_rows[i] = 0;
        state.module_lamps[i] = 0;
        state.module_name_labels[i] = 0;
        (void)snprintf(
            state.module_meta_status[i],
            sizeof(state.module_meta_status[i]),
            "%s",
            sc_i18n_string_get(SC_I18N_STATUS_NO_META)
        );
        (void)snprintf(
            state.module_catalog_status[i],
            sizeof(state.module_catalog_status[i]),
            "%s",
            sc_i18n_string_get(SC_I18N_STATUS_NO_CATALOG)
        );
        (void)snprintf(
            state.module_values_status[i],
            sizeof(state.module_values_status[i]),
            "%s",
            sc_i18n_string_get(SC_I18N_STATUS_NO_VALUES)
        );
        (void)snprintf(
            state.module_param_probe_status[i],
            sizeof(state.module_param_probe_status[i]),
            "%s",
            sc_i18n_string_get(SC_I18N_STATUS_NO_PROBE)
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
