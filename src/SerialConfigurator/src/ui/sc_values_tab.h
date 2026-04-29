#ifndef SC_VALUES_TAB_H
#define SC_VALUES_TAB_H

/*
 * Phase 8.6: third top-level notebook page - "Values".
 *
 * Holds an inner GtkNotebook with one sub-tab per *detected* module
 * (ECU / Clocks / OilAndSpeed). Sub-tabs are added on
 * @ref sc_values_tab_rebuild and removed when the module disappears
 * from the next detection cycle.
 *
 * Each module sub-tab is a vertically-scrolling form. Parameters are
 * grouped by their descriptor `group` token (snake-case on the wire,
 * Title-Case on screen) and rendered as
 *
 *   [<id>]  [GtkSpinButton]  [GtkScale]  [<status>]
 *
 * where the spin button and slider share a single GtkAdjustment so
 * they stay in sync. Range comes from the descriptor (min/max),
 * default from the descriptor's default_value; clamping at the
 * widget level is the first line of defence (the firmware enforces
 * the same bounds at SET_PARAM time).
 *
 * Footer per sub-tab: [Apply staged] [Commit] [Revert]. One COMMIT
 * for the whole module is the firmware-natural granularity (cross-
 * field validation runs over the whole staging mirror at once).
 */

#include <gtk/gtk.h>

#include "sc_ui_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the Values tab body and return its root widget. Stores
 *        the root pointer in @p state->values_tab_root for subsequent
 *        @ref sc_values_tab_rebuild calls.
 */
GtkWidget *sc_values_tab_build(AppState *state);

/**
 * @brief Re-render the Values tab from the current @p state->core
 *        detection cache. Call after every detection cycle (and on
 *        the disconnected-reset path with @p detected_any = false).
 */
void sc_values_tab_rebuild(AppState *state, bool detected_any);

#ifdef __cplusplus
}
#endif

#endif /* SC_VALUES_TAB_H */
