#ifndef SC_FLASH_TAB_H
#define SC_FLASH_TAB_H

/*
 * Notebook page 2: the Flash tab. Phase 6.1 lands the empty
 * scaffolding (placeholder label gated by detection state). Phase 6.2
 * replaces the placeholder with one section per detected in-scope
 * module; subsequent slices wire format checks, manifest preflight,
 * progress bars, and the Flash button itself.
 */

#include <gtk/gtk.h>

#include "sc_ui_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the Flash-tab body widget. Stores the resulting
 *        container in @p state->flash_tab_root so subsequent
 *        sensitivity refreshes have a target. Returns the same
 *        widget the caller can attach to the notebook.
 */
GtkWidget *sc_flash_tab_build(AppState *state);

/**
 * @brief Toggle @p state->flash_tab_root sensitivity based on
 *        whether any in-scope module is currently detected.
 *        Adjustometer is excluded by construction (not in
 *        SC_MODULE_COUNT — see v1.32 lock); no special filtering
 *        needed here.
 */
void sc_flash_tab_refresh_sensitivity(AppState *state);

/**
 * @brief Rebuild the per-module flash sections in the second tab
 *        (Phase 6.2). Called whenever the detection state changes
 *        — adds a section for every in-scope module that is
 *        currently detected, removes sections for modules that are
 *        no longer detected, and falls back to the placeholder when
 *        no module is detected. Persisted paths
 *        (`state->flash_paths`) are re-applied to every freshly
 *        rebuilt section so the operator does not have to re-pick
 *        paths after every Detect.
 */
void sc_flash_tab_rebuild_sections(AppState *state);

/**
 * @brief Toggle the global "flash in progress" lock. While locked,
 *        every Flash button, the Detect button, and every section's
 *        file pickers go insensitive. The active section's status
 *        field stays live. Phase 6.5 wires the real flash flow into
 *        this lock; Phase 6.2 only exercises the plumbing from the
 *        stubbed Flash button.
 */
void sc_flash_tab_set_lock(AppState *state, bool locked);

#ifdef __cplusplus
}
#endif

#endif /* SC_FLASH_TAB_H */
