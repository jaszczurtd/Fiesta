#ifndef SC_MAP_TAB_H
#define SC_MAP_TAB_H

/*
 * Phase 8.7: fourth top-level notebook page - "Map".
 *
 * Polls the ECU's SC_GET_GPS endpoint on a 2-second main-loop timer
 * (well under the SC framing budget for a single command) and renders
 * the latest position on a libshumate map widget. Falls back to a
 * static placeholder when libshumate is not compiled in - the tab
 * still appears so the user gets a consistent UI surface across hosts.
 *
 * Concurrency: polling runs entirely on the GTK main thread. We rely
 * on AppState->detection_in_progress / flash_in_progress to gate the
 * blocking serial transaction so it never overlaps the detection
 * worker or the flash orchestrator.
 */

#include <gtk/gtk.h>
#include <stdbool.h>

#include "sc_ui_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the Map tab body and return its root widget. Caches
 *        the root pointer and internal state in @p state.
 */
GtkWidget *sc_map_tab_build(AppState *state);

/**
 * @brief Notify the Map tab that the connection state changed. When
 *        connected, polling resumes; when not, the marker is hidden
 *        and the timer paused so the tab stays responsive without
 *        spamming the (now absent) device.
 */
void sc_map_tab_set_connected(AppState *state, bool connected);

/** @brief Release any widgets and timers owned by the Map tab. */
void sc_map_tab_dispose(AppState *state);

#ifdef __cplusplus
}
#endif

#endif /* SC_MAP_TAB_H */
