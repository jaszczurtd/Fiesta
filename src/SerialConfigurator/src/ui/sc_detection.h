#ifndef SC_DETECTION_H
#define SC_DETECTION_H

/*
 * Async detection orchestration. Owns the worker thread that drives
 * sc_core_detect_modules + the per-module metadata refresh, the
 * GTask-based main-thread callback that publishes the result, the
 * Detect button click handler, and the Disconnect/reset path. Status
 * strings the worker produces are written into AppState's
 * module_*_status arrays via the modules-view setters.
 */

#include <gtk/gtk.h>
#include <stdbool.h>

#include "sc_ui_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reset the application to its idle state: clear detected
 *        modules, clear selection, restore the Detect button label
 *        and the placeholder log message. When @p by_user_request is
 *        true the host first sends `SC_BYE` to each detected module so
 *        firmware can drop the active SC session and resume debug
 *        logging, then writes the disconnect log. Otherwise it reads
 *        the plain idle prompt. Safe to call before the widget tree is
 *        fully built (every widget reference is NULL-checked).
 */
void sc_detection_reset_connection(AppState *state, bool by_user_request);

/**
 * @brief Kick off detection in a background worker. No-op if a
 *        detection is already in flight. Marshals progress + result
 *        back to the GTK main loop via GTask.
 */
void sc_detection_start_async(AppState *state);

/**
 * @brief Signal handler for the Detect button. Toggles between
 *        "start detection" and "disconnect" based on the current
 *        connected state.
 */
void sc_detection_on_detect_clicked(GtkButton *button, gpointer user_data);

#ifdef __cplusplus
}
#endif

#endif /* SC_DETECTION_H */
