#ifndef SC_TESTS_TAB_H
#define SC_TESTS_TAB_H

/*
 * Values tab, ECU module: the "Tests" sub-tab.
 *
 * Built from what the ECU reports (SC_TEST_LIST / INFO / PARAM), so a
 * production image without functional tests shows "No tests available".
 * Each test gets a card with its description, its runtime parameters and a
 * Run button; the header runs the whole sequence, skips or stops, and a
 * status panel shows the running test, its sequence step, the demanded and
 * measured actuator position and the test's own progress values.
 *
 * The panel polls SC_TEST_STATUS while it is visible, and always while a
 * test started here runs: the ECU stops a configurator test whose host goes
 * quiet, so the polling is what keeps it alive.
 */

#include <gtk/gtk.h>
#include <stddef.h>

#include "sc_ui_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the Tests sub-tab for the module at @p module_index.
 *
 * Reads the catalog synchronously. The returned widget owns its state and
 * its poll timer; both go away with the widget on the next rebuild.
 */
GtkWidget *sc_tests_tab_build(AppState *state, size_t module_index);

#ifdef __cplusplus
}
#endif

#endif /* SC_TESTS_TAB_H */
