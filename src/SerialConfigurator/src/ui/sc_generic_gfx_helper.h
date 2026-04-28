#ifndef SC_GENERIC_GFX_HELPER_H
#define SC_GENERIC_GFX_HELPER_H

/*
 * Catch-all for non-domain-specific GTK helpers used across the UI
 * subviews. Anything that touches widgets but does not belong to a
 * specific subview (modules view, flash tab, detection orchestrator)
 * lands here.
 */

#include <gtk/gtk.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Install the application's CSS provider on the default GDK
 *        display. Idempotent - safe to call once at app startup.
 */
void sc_gfx_install_css(void);

/**
 * @brief Toggle a status indicator widget between green ("detected")
 *        and red ("not detected"). The widget must already carry the
 *        `status-lamp` CSS class; this helper only flips the
 *        red/green colour modifier.
 */
void sc_gfx_set_lamp_state(GtkWidget *lamp, bool detected);

#ifdef __cplusplus
}
#endif

#endif /* SC_GENERIC_GFX_HELPER_H */
