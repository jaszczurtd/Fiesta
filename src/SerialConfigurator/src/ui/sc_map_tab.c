/*
 * Phase 8.7 - Map tab implementation.
 *
 * Live GPS view sourced from the ECU's SC_GET_GPS endpoint. The widget
 * tree is:
 *
 *   GtkBox (vertical)
 *     ├── ShumateSimpleMap        (libshumate; OSM tiles by default)
 *     │     └── ShumateMarkerLayer
 *     │           └── ShumateMarker (round dot at current position)
 *     └── GtkLabel "Lat ... Lon ... km/h"  / placeholder
 *
 * Polling: a g_timeout_add main-loop timer (SC_MAP_POLL_INTERVAL_MS)
 * issues sc_gps_get against the ECU module if the AppState gate
 * allows it (connected, no detection in flight, no flash in flight).
 * SC framing per command is a few ms over USB-CDC, so blocking the
 * UI for the round-trip is acceptable and avoids the locking issues
 * of running serial I/O in parallel with the detection worker.
 *
 * When libshumate is not available at build time the tab degrades to
 * a single label so the rest of the GUI still ships - SC_HAVE_SHUMATE
 * is set by CMake when pkg_check_modules(SHUMATE shumate-1.0) succeeds.
 */

#include "sc_map_tab.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sc_core.h"
#include "sc_gps.h"
#include "sc_i18n.h"

#ifdef SC_HAVE_SHUMATE
#include <shumate/shumate.h>
#endif

/* Keep host polling cadence aligned with ECU GPS_UPDATE (4 s) so
 * SC_GET_GPS requests do not run twice as often as firmware refresh. */
#define SC_MAP_POLL_INTERVAL_MS 4000u

/* Default centre: Warsaw, PL. Used only as a sane viewport target
 * before the first GPS fix arrives. Replaced as soon as we receive
 * available=1. */
#define SC_MAP_DEFAULT_LAT 52.2297
#define SC_MAP_DEFAULT_LON 21.0122
#define SC_MAP_DEFAULT_ZOOM 14.0

typedef struct ScMapTabState {
    AppState *app;
    GtkWidget *status_label;
    guint poll_timeout_id;
    bool have_fix;
    double last_latitude_deg;
    double last_longitude_deg;
#ifdef SC_HAVE_SHUMATE
    GtkWidget *recenter_button;
    ShumateSimpleMap *simple_map;
    ShumateMapSourceRegistry *registry;
    ShumateMarkerLayer *marker_layer;
    ShumateMarker *marker;
    ShumateViewport *viewport;
#endif
} ScMapTabState;

static ScMapTabState *sc_map_state(const AppState *state)
{
    if (state == 0) {
        return 0;
    }
    return (ScMapTabState *)state->map_tab_state;
}

/* Find the ECU module entry by display name. We treat ECU as the
 * single GPS source - other modules don't ship gps.c. Returns
 * SC_MODULE_COUNT when nothing usable is detected (ambiguous or
 * absent). */
static size_t find_ecu_module(const AppState *state)
{
    if (state == 0) {
        return SC_MODULE_COUNT;
    }
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        const ScModuleStatus *st = sc_core_module_status(&state->core, i);
        if (st == 0 || !st->detected || st->target_ambiguous) {
            continue;
        }
        if (strcmp(st->display_name, SC_MODULE_ECU) == 0) {
            return i;
        }
    }
    return SC_MODULE_COUNT;
}

static void sc_map_set_status(ScMapTabState *m, const char *text)
{
    if (m == 0 || m->status_label == 0 || text == 0) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(m->status_label), text);
}

#ifdef SC_HAVE_SHUMATE
static void sc_map_set_recenter_enabled(ScMapTabState *m, bool enabled)
{
    if (m == 0 || m->recenter_button == 0) {
        return;
    }
    gtk_widget_set_sensitive(m->recenter_button, enabled ? TRUE : FALSE);
}

static void sc_map_center_on_last_fix(ScMapTabState *m)
{
    if (m == 0 || m->viewport == 0 || !m->have_fix) {
        return;
    }
    shumate_location_set_location(
        SHUMATE_LOCATION(m->viewport),
        m->last_latitude_deg,
        m->last_longitude_deg
    );
}

static void on_recenter_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ScMapTabState *m = (ScMapTabState *)user_data;
    sc_map_center_on_last_fix(m);
}

static void sc_map_apply_fix(ScMapTabState *m, const ScGpsSnapshot *snap)
{
    if (m == 0 || snap == 0 || !snap->available) {
        return;
    }

    m->last_latitude_deg = snap->latitude_deg;
    m->last_longitude_deg = snap->longitude_deg;

    if (m->marker != 0) {
        shumate_location_set_location(
            SHUMATE_LOCATION(m->marker),
            snap->latitude_deg,
            snap->longitude_deg
        );
        gtk_widget_set_visible(GTK_WIDGET(m->marker), TRUE);
    }

    if (m->viewport != 0) {
        /* Centre the viewport on the fix but only on the very first
         * one - users panning the map manually should not have their
         * view yanked back to the marker on every tick. */
        if (!m->have_fix) {
            shumate_location_set_location(
                SHUMATE_LOCATION(m->viewport),
                snap->latitude_deg,
                snap->longitude_deg
            );
            shumate_viewport_set_zoom_level(m->viewport, 16.0);
        }
    }
    m->have_fix = true;
    sc_map_set_recenter_enabled(m, true);

    char buf[160];
    (void)snprintf(buf, sizeof(buf),
                   sc_i18n_string_get(SC_I18N_MAP_STATUS_FMT),
                   snap->latitude_deg,
                   snap->longitude_deg,
                   snap->speed_kmh);
    sc_map_set_status(m, buf);
}
#endif /* SC_HAVE_SHUMATE */

static gboolean sc_map_poll_tick(gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    ScMapTabState *m = sc_map_state(state);
    if (state == 0 || m == 0) {
        return G_SOURCE_REMOVE;
    }

    if (!state->connected) {
#ifdef SC_HAVE_SHUMATE
        sc_map_set_recenter_enabled(m, false);
#endif
        sc_map_set_status(m, sc_i18n_string_get(SC_I18N_MAP_DISCONNECTED));
        return G_SOURCE_CONTINUE;
    }
    /* Yield to the detection worker and the flash orchestrator -
     * both can hold the serial port exclusively. */
    if (state->detection_in_progress || state->flash_in_progress) {
        return G_SOURCE_CONTINUE;
    }

    const size_t ecu_idx = find_ecu_module(state);
    if (ecu_idx >= SC_MODULE_COUNT) {
#ifdef SC_HAVE_SHUMATE
        sc_map_set_recenter_enabled(m, false);
#endif
        sc_map_set_status(m, sc_i18n_string_get(SC_I18N_MAP_PLACEHOLDER));
        return G_SOURCE_CONTINUE;
    }

    ScCommandResult result;
    char log_buf[256];
    log_buf[0] = '\0';
    if (!sc_gps_get(&state->core, ecu_idx, &result,
                    log_buf, sizeof(log_buf))) {
        /* Transport hiccup is expected when the cable is yanked
         * mid-poll; stay quiet and try again next tick. */
        return G_SOURCE_CONTINUE;
    }

    ScGpsSnapshot snap;
    char err[160];
    if (!sc_gps_parse_result(&result, &snap, err, sizeof(err))) {
        return G_SOURCE_CONTINUE;
    }

    if (!snap.available) {
#ifdef SC_HAVE_SHUMATE
        sc_map_set_recenter_enabled(m, false);
#endif
        sc_map_set_status(m, sc_i18n_string_get(SC_I18N_MAP_WAITING_FIX));
        return G_SOURCE_CONTINUE;
    }

#ifdef SC_HAVE_SHUMATE
    sc_map_apply_fix(m, &snap);
#else
    char buf[160];
    (void)snprintf(buf, sizeof(buf),
                   sc_i18n_string_get(SC_I18N_MAP_STATUS_FMT),
                   snap.latitude_deg,
                   snap.longitude_deg,
                   snap.speed_kmh);
    sc_map_set_status(m, buf);
#endif

    return G_SOURCE_CONTINUE;
}

#ifdef SC_HAVE_SHUMATE
static void sc_map_build_shumate(ScMapTabState *m, GtkWidget *parent_box)
{
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(parent_box), toolbar);

    m->recenter_button = gtk_button_new_with_label(
        sc_i18n_string_get(SC_I18N_MAP_BTN_RECENTER));
    g_signal_connect(m->recenter_button, "clicked",
                     G_CALLBACK(on_recenter_clicked), m);
    gtk_widget_set_sensitive(m->recenter_button, FALSE);
    gtk_box_append(GTK_BOX(toolbar), m->recenter_button);

    m->simple_map = shumate_simple_map_new();
    gtk_widget_set_hexpand(GTK_WIDGET(m->simple_map), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(m->simple_map), TRUE);
    gtk_box_append(GTK_BOX(parent_box), GTK_WIDGET(m->simple_map));

    /* The default OSM tile source ships with libshumate and is
     * sufficient for a road-level GPS view. Using the registry
     * default keeps us forward-compatible with shumate adding more
     * tile sources without us pinning a specific id string. */
    m->registry = shumate_map_source_registry_new_with_defaults();
    ShumateMapSource *src = shumate_map_source_registry_get_by_id(
        m->registry, SHUMATE_MAP_SOURCE_OSM_MAPNIK);
    if (src != 0) {
        shumate_simple_map_set_map_source(m->simple_map, src);
    }

    m->viewport = shumate_simple_map_get_viewport(m->simple_map);
    if (m->viewport != 0) {
        shumate_viewport_set_zoom_level(m->viewport, SC_MAP_DEFAULT_ZOOM);
        shumate_location_set_location(
            SHUMATE_LOCATION(m->viewport),
            SC_MAP_DEFAULT_LAT,
            SC_MAP_DEFAULT_LON
        );
    }

    m->marker_layer = shumate_marker_layer_new(m->viewport);
    shumate_simple_map_add_overlay_layer(
        m->simple_map, SHUMATE_LAYER(m->marker_layer));

    m->marker = shumate_marker_new();
    /* A simple visible dot - libshumate ≥ 1.2 will render the marker
     * with a CSS-styleable child if we pack one in, otherwise it
     * falls back to a default glyph. */
    GtkWidget *dot = gtk_image_new_from_icon_name("mark-location-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(dot), 24);
    shumate_marker_set_child(m->marker, dot);
    gtk_widget_set_visible(GTK_WIDGET(m->marker), FALSE);
    shumate_marker_layer_add_marker(m->marker_layer, m->marker);
}
#endif /* SC_HAVE_SHUMATE */

GtkWidget *sc_map_tab_build(AppState *state)
{
    if (state == 0) {
        return gtk_label_new("");
    }

    ScMapTabState *m = g_new0(ScMapTabState, 1u);
    m->app = state;
    state->map_tab_state = m;

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    gtk_widget_set_margin_top(root, 12);
    gtk_widget_set_margin_bottom(root, 12);

#ifdef SC_HAVE_SHUMATE
    sc_map_build_shumate(m, root);
#else
    GtkWidget *placeholder = gtk_label_new(
        sc_i18n_string_get(SC_I18N_MAP_UNAVAILABLE));
    gtk_widget_set_hexpand(placeholder, TRUE);
    gtk_widget_set_vexpand(placeholder, TRUE);
    gtk_box_append(GTK_BOX(root), placeholder);
#endif

    m->status_label = gtk_label_new(sc_i18n_string_get(SC_I18N_MAP_PLACEHOLDER));
    gtk_label_set_xalign(GTK_LABEL(m->status_label), 0.0f);
    gtk_box_append(GTK_BOX(root), m->status_label);

    state->map_tab_root = root;

    /* Polling is permanent - the tick callback itself short-circuits
     * when disconnected. This keeps the start/stop logic in one place
     * (the gate check) and avoids racing the connection state flag
     * against timer add/remove. */
    m->poll_timeout_id = g_timeout_add(SC_MAP_POLL_INTERVAL_MS,
                                       sc_map_poll_tick, state);
    return root;
}

void sc_map_tab_set_connected(AppState *state, bool connected)
{
    ScMapTabState *m = sc_map_state(state);
    if (m == 0) {
        return;
    }
    if (!connected) {
        m->have_fix = false;
    #ifdef SC_HAVE_SHUMATE
        sc_map_set_recenter_enabled(m, false);
    #endif
#ifdef SC_HAVE_SHUMATE
        if (m->marker != 0) {
            gtk_widget_set_visible(GTK_WIDGET(m->marker), FALSE);
        }
#endif
        sc_map_set_status(m, sc_i18n_string_get(SC_I18N_MAP_DISCONNECTED));
    }
}

void sc_map_tab_dispose(AppState *state)
{
    ScMapTabState *m = sc_map_state(state);
    if (m == 0) {
        return;
    }
    if (m->poll_timeout_id != 0u) {
        g_source_remove(m->poll_timeout_id);
        m->poll_timeout_id = 0u;
    }
#ifdef SC_HAVE_SHUMATE
    g_clear_object(&m->registry);
#endif
    g_free(m);
    state->map_tab_state = 0;
}
