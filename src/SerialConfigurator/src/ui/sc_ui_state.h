#ifndef SC_UI_STATE_H
#define SC_UI_STATE_H

/*
 * Private state header shared between the UI subview translation units
 * (sc_app.c, sc_modules_view.c, sc_detection.c, sc_flash_tab.c,
 * sc_generic_gfx_helper.c). Not part of the public sc_app API; do not
 * include from the CLI or core library.
 *
 * Each subview owns the widgets and helpers tied to its panel and only
 * touches the subset of AppState it needs. The struct stays a single
 * type because GTK widget pointers, scratch status strings, and the
 * detection cache all share the same lifetime — splitting per-subview
 * structs would buy nothing here.
 */

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stddef.h>

#include "sc_core.h"
#include "sc_flash_paths.h"
#include "sc_module_details.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_DETECTION_LOG_MAX 16384u
#define UI_AUTO_REFRESH_PARAM_PROBE 0

typedef struct AppState {
    ScCore core;
    GtkTextBuffer *log_buffer;
    GtkWidget *detect_button;
    GtkWidget *module_list;
    GtkWidget *module_rows[SC_MODULE_COUNT];
    GtkWidget *module_lamps[SC_MODULE_COUNT];
    GtkWidget *module_name_labels[SC_MODULE_COUNT];
    ScModuleDetailsView details_view;
    /* Phase 6.1+: notebook tab 2 root widget. Sensitive iff at least
     * one in-scope module is detected. Phase 6.2 populates the body
     * with per-module sections at every detection cycle. */
    GtkWidget *flash_tab_root;
    /* Phase 6.2: per-module flash UI substate (paths persisted to
     * flash-paths.json + global flash-in-progress lock flag). */
    ScFlashPaths flash_paths;
    bool flash_in_progress;
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

/* All user-facing strings live in sc_i18n.{h,c}. Subviews call
 * `sc_i18n_string_get(SC_I18N_*)` instead of referencing extern label
 * constants from this header (legacy `k_*_label` externs were
 * dropped in v1.35). */

#ifdef __cplusplus
}
#endif

#endif /* SC_UI_STATE_H */
