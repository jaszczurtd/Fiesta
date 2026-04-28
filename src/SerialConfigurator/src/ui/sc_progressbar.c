#include "sc_progressbar.h"

typedef struct {
    double fraction; /* always clamped to [0, 1] */
    int preferred_height_px;
} ScProgressBarState;

static const char *const k_state_key = "sc-progressbar-state";

static ScProgressBarState *progress_state(GtkWidget *progress_bar)
{
    if (progress_bar == NULL) {
        return NULL;
    }
    return (ScProgressBarState *)g_object_get_data(
        G_OBJECT(progress_bar), k_state_key);
}

static void draw_progress(GtkDrawingArea *area, cairo_t *cr, int width,
                          int height, gpointer user_data)
{
    (void)area;
    ScProgressBarState *st = (ScProgressBarState *)user_data;
    if (width <= 0 || height <= 0) {
        return;
    }
    double frac = (st != NULL) ? st->fraction : 0.0;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    int bar_h = (st != NULL && st->preferred_height_px > 0)
                    ? st->preferred_height_px
                    : height;
    if (bar_h > height) {
        bar_h = height;
    }
    if (bar_h < 1) {
        bar_h = 1;
    }
    const double y = ((double)height - (double)bar_h) * 0.5;

    /* Trough */
    cairo_set_source_rgb(cr, 0.86, 0.86, 0.86);
    cairo_rectangle(cr, 0.0, y, (double)width, (double)bar_h);
    cairo_fill(cr);

    /* Fill */
    const double fill_w = frac * (double)width;
    if (fill_w > 0.0) {
        cairo_set_source_rgb(cr, 0.27, 0.60, 0.95);
        cairo_rectangle(cr, 0.0, y, fill_w, (double)bar_h);
        cairo_fill(cr);
    }

    /* Border */
    if (width >= 2 && bar_h >= 2) {
        cairo_set_source_rgb(cr, 0.58, 0.58, 0.58);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, 0.5, y + 0.5, (double)width - 1.0,
                        (double)bar_h - 1.0);
        cairo_stroke(cr);
    }
}

GtkWidget *sc_progressbar_new(int height_px)
{
    if (height_px <= 0) {
        height_px = 18;
    }

    GtkWidget *bar = gtk_drawing_area_new();
    gtk_widget_set_hexpand(bar, TRUE);
    gtk_widget_set_vexpand(bar, FALSE);
    gtk_widget_set_valign(bar, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(bar, FALSE);
    gtk_widget_set_size_request(bar, -1, height_px);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(bar), height_px);

    ScProgressBarState *st = g_new0(ScProgressBarState, 1);
    st->fraction = 0.0;
    st->preferred_height_px = height_px;
    g_object_set_data_full(G_OBJECT(bar), k_state_key, st, g_free);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(bar), draw_progress,
                                   st, NULL);
    return bar;
}

void sc_progressbar_set_fraction(GtkWidget *progress_bar, double fraction)
{
    ScProgressBarState *st = progress_state(progress_bar);
    if (st == NULL) {
        return;
    }
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    st->fraction = fraction;
    gtk_widget_set_visible(progress_bar, TRUE);
    gtk_widget_queue_draw(progress_bar);
}

double sc_progressbar_get_fraction(GtkWidget *progress_bar)
{
    ScProgressBarState *st = progress_state(progress_bar);
    if (st == NULL) {
        return 0.0;
    }
    return st->fraction;
}
