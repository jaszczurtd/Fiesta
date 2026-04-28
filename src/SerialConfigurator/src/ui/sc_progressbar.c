#include "sc_progressbar.h"

#include <math.h>

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

/* Resolve the foreground color of @p widget — used as the seed for
 * the trough / border tints so the bar adapts to dark mode and to
 * arbitrary user themes. */
static void resolve_fg_color(GtkWidget *widget, GdkRGBA *out)
{
    out->red = 0.0; out->green = 0.0; out->blue = 0.0; out->alpha = 1.0;
    if (widget == NULL) {
        return;
    }
#if GTK_CHECK_VERSION(4, 10, 0)
    gtk_widget_get_color(widget, out);
#else
    GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
    if (ctx != NULL) {
        gtk_style_context_get_color(ctx, out);
    }
#endif
}

/* Resolve a "brand" fill color. We look up themes' named accent
 * colors first (libadwaita's @accent_bg_color, then the legacy
 * @theme_selected_bg_color); when nothing matches we fall back to a
 * neutral blue tuned to read on both light and dark Mint Cinnamon
 * themes. */
static void resolve_fill_color(GtkWidget *widget, GdkRGBA *out)
{
    /* Tuned blue — visible on Mint-Y light, Mint-Y-Dark, Adwaita
     * (light & dark). Used as the fallback when the theme exposes
     * no accent color. */
    out->red = 0.27; out->green = 0.60; out->blue = 0.95; out->alpha = 1.0;

    if (widget == NULL) {
        return;
    }
    /* gtk_widget_get_style_context + gtk_style_context_lookup_color
     * are both deprecated since GTK 4.10 but remain functional and
     * are the only public way to read named theme colors (incl.
     * accent / selection) without pulling in libadwaita. Single
     * BEGIN/END pair — the lookup loop falls through to END on
     * either match (via break) or no-match. */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
    if (ctx != NULL) {
        static const char *const k_candidates[] = {
            "accent_bg_color",         /* libadwaita */
            "theme_selected_bg_color", /* legacy GTK */
        };
        for (size_t i = 0u; i < G_N_ELEMENTS(k_candidates); ++i) {
            GdkRGBA c;
            if (gtk_style_context_lookup_color(ctx, k_candidates[i], &c)) {
                *out = c;
                break;
            }
        }
    }
G_GNUC_END_IGNORE_DEPRECATIONS
}

static void cairo_set_rgba_alpha(cairo_t *cr, const GdkRGBA *base,
                                 double alpha_mul)
{
    cairo_set_source_rgba(cr, base->red, base->green, base->blue,
                          base->alpha * alpha_mul);
}

static void draw_progress(GtkDrawingArea *area, cairo_t *cr, int width,
                          int height, gpointer user_data)
{
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

    GdkRGBA fg, fill;
    resolve_fg_color(GTK_WIDGET(area), &fg);
    resolve_fill_color(GTK_WIDGET(area), &fill);

    /* Trough: fg tinted at 12 % — invisible enough on both light
     * and dark themes to read as a track without competing with the
     * fill. */
    cairo_set_rgba_alpha(cr, &fg, 0.12);
    cairo_rectangle(cr, 0.0, y, (double)width, (double)bar_h);
    cairo_fill(cr);

    /* Fill: theme accent (or fallback blue) at full opacity. */
    const double fill_w = frac * (double)width;
    if (fill_w > 0.0) {
        cairo_set_source_rgba(cr, fill.red, fill.green, fill.blue,
                              fill.alpha);
        cairo_rectangle(cr, 0.0, y, fill_w, (double)bar_h);
        cairo_fill(cr);
    }

    /* Border: fg tinted at 35 % — visible outline on dark mode where
     * a 0.58-grey border would disappear. */
    if (width >= 2 && bar_h >= 2) {
        cairo_set_rgba_alpha(cr, &fg, 0.35);
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
