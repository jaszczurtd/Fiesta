#ifndef SC_MODULES_VIEW_H
#define SC_MODULES_VIEW_H

/*
 * Notebook page 1: the legacy Modules-and-log view. Builds the
 * widget tree (top row, left module list, right details panel,
 * bottom log) and exposes the per-module status setters and refresh
 * helpers that the detection orchestrator drives.
 */

#include <gtk/gtk.h>

#include "sc_ui_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the Modules-tab widget tree and register all GTK
 *        signal handlers that own its state. The function attaches
 *        widgets directly to @p parent_box (typically the notebook
 *        page container) and stores every widget reference the rest
 *        of the UI cares about into @p state.
 */
void sc_modules_view_build(AppState *state, GtkWidget *parent_box);

void sc_modules_view_set_meta_status(AppState *state,
                                     size_t module_index,
                                     const char *message);
void sc_modules_view_set_catalog_status(AppState *state,
                                        size_t module_index,
                                        const char *message);
void sc_modules_view_set_values_status(AppState *state,
                                       size_t module_index,
                                       const char *message);
void sc_modules_view_set_param_probe_status(AppState *state,
                                            size_t module_index,
                                            const char *message);
void sc_modules_view_set_placeholder(AppState *state, const char *message);

void sc_modules_view_refresh_lamps(AppState *state);
void sc_modules_view_refresh_details(AppState *state);
void sc_modules_view_select_first_detected(AppState *state);

/**
 * @brief Signal handler for the module list's `row-selected` event.
 *        Exposed because `g_signal_connect` is wired by the orchestrator
 *        in sc_modules_view_build, but we keep the callback in the
 *        public surface so unit tests / future code can drive it
 *        deterministically.
 */
void sc_modules_view_on_row_selected(GtkListBox *box,
                                     GtkListBoxRow *row,
                                     gpointer user_data);

#ifdef __cplusplus
}
#endif

#endif /* SC_MODULES_VIEW_H */
