#ifndef SC_PROGRESSBAR_H
#define SC_PROGRESSBAR_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a reusable custom progress-bar widget backed by
 *        GtkDrawingArea + cairo.
 *
 * The returned widget is a GtkWidget*, so callers can pack it like
 * any other GTK child. Height <= 0 falls back to 18 px.
 */
GtkWidget *sc_progressbar_new(int height_px);

/**
 * @brief Set the progress fraction in [0, 1] and schedule redraw.
 *
 * Values outside the range are clamped.
 */
void sc_progressbar_set_fraction(GtkWidget *progress_bar, double fraction);

/**
 * @brief Read current progress fraction in [0, 1].
 *
 * Returns 0.0 for NULL or non-sc_progressbar widgets.
 */
double sc_progressbar_get_fraction(GtkWidget *progress_bar);

#ifdef __cplusplus
}
#endif

#endif /* SC_PROGRESSBAR_H */
