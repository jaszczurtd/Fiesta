#include <gtk/gtk.h>

#include "sc_app.h"
#include "sc_core.h"

typedef struct AppState {
    ScCore core;
    GtkTextBuffer *log_buffer;
    GtkWidget *detect_button;
    GtkWidget *module_lamps[SC_MODULE_COUNT];
    bool connected;
    bool detection_in_progress;
} AppState;

typedef struct DetectionResult {
    ScCore core;
    char *log_text;
} DetectionResult;

#define UI_DETECTION_LOG_MAX 8192u

static const char *k_detect_label = "Detect Fiesta Modules";
static const char *k_disconnect_label = "Disconnect";
static const char *k_detecting_label = "Detecting...";
static const char *k_idle_log_message =
    "Press \"Detect Fiesta Modules\" to send HELLO to connected modules.\n";

static void detection_result_free(DetectionResult *result)
{
    if (result == 0) {
        return;
    }

    g_free(result->log_text);
    g_free(result);
}

static void set_lamp_state(GtkWidget *lamp, bool detected)
{
    if (lamp == 0) {
        return;
    }

    gtk_widget_remove_css_class(lamp, "status-red");
    gtk_widget_remove_css_class(lamp, "status-green");
    gtk_widget_add_css_class(lamp, detected ? "status-green" : "status-red");
}

static void refresh_module_lamps(AppState *state)
{
    if (state == 0) {
        return;
    }

    const size_t module_count = sc_core_module_count();
    for (size_t i = 0u; i < module_count; ++i) {
        const ScModuleStatus *status = sc_core_module_status(&state->core, i);
        if (status == 0) {
            continue;
        }

        set_lamp_state(state->module_lamps[i], status->detected);
    }
}

static void reset_connection_state(AppState *state, bool by_user_request)
{
    if (state == 0) {
        return;
    }

    sc_core_reset_detection(&state->core);
    refresh_module_lamps(state);
    state->connected = false;
    state->detection_in_progress = false;

    if (state->detect_button != 0) {
        gtk_widget_set_sensitive(state->detect_button, TRUE);
        gtk_button_set_label(GTK_BUTTON(state->detect_button), k_detect_label);
    }

    if (state->log_buffer != 0) {
        if (by_user_request) {
            gtk_text_buffer_set_text(
                state->log_buffer,
                "Disconnected. Application state has been reset.\n"
                "Press \"Detect Fiesta Modules\" to start detection again.\n",
                -1
            );
        } else {
            gtk_text_buffer_set_text(state->log_buffer, k_idle_log_message, -1);
        }
    }
}

static void run_detection_worker(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    (void)source_object;
    (void)task_data;
    (void)cancellable;

    DetectionResult *result = g_new0(DetectionResult, 1u);
    result->log_text = g_malloc0(UI_DETECTION_LOG_MAX);

    sc_core_init(&result->core);
    sc_core_detect_modules(&result->core, result->log_text, UI_DETECTION_LOG_MAX);

    g_task_return_pointer(task, result, (GDestroyNotify)detection_result_free);
}

static void on_detection_finished(GObject *source_object, GAsyncResult *async_result, gpointer user_data)
{
    (void)source_object;

    AppState *state = (AppState *)user_data;
    if (state == 0) {
        return;
    }

    DetectionResult *result = g_task_propagate_pointer(G_TASK(async_result), 0);
    state->detection_in_progress = false;

    if (result == 0) {
        if (state->detect_button != 0) {
            gtk_widget_set_sensitive(state->detect_button, TRUE);
            gtk_button_set_label(GTK_BUTTON(state->detect_button), k_detect_label);
        }
        if (state->log_buffer != 0) {
            gtk_text_buffer_set_text(
                state->log_buffer,
                "Detection failed: internal task error.\n",
                -1
            );
        }
        refresh_module_lamps(state);
        state->connected = false;
        return;
    }

    state->core = result->core;
    refresh_module_lamps(state);

    if (state->log_buffer != 0 && result->log_text != 0) {
        gtk_text_buffer_set_text(state->log_buffer, result->log_text, -1);
    }

    if (state->detect_button != 0) {
        gtk_widget_set_sensitive(state->detect_button, TRUE);
        gtk_button_set_label(GTK_BUTTON(state->detect_button), k_disconnect_label);
    }

    state->connected = true;
    detection_result_free(result);
}

static void start_detection_async(AppState *state)
{
    if (state == 0 || state->detection_in_progress) {
        return;
    }

    state->detection_in_progress = true;
    state->connected = false;
    sc_core_reset_detection(&state->core);
    refresh_module_lamps(state);

    if (state->log_buffer != 0) {
        gtk_text_buffer_set_text(
            state->log_buffer,
            "Detection started... please wait.\n",
            -1
        );
    }

    if (state->detect_button != 0) {
        gtk_button_set_label(GTK_BUTTON(state->detect_button), k_detecting_label);
        gtk_widget_set_sensitive(state->detect_button, FALSE);
    }

    GTask *task = g_task_new(0, 0, on_detection_finished, state);
    g_task_run_in_thread(task, run_detection_worker);
    g_object_unref(task);
}

static void on_detect_clicked(GtkButton *button, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    if (state == 0 || state->log_buffer == 0) {
        return;
    }

    if (state->connected) {
        reset_connection_state(state, true);
        return;
    }

    (void)button;
    start_detection_async(state);
}

static void install_css(void)
{
    static const char *css =
        ".status-lamp {"
        "  min-width: 14px;"
        "  min-height: 14px;"
        "  border-radius: 7px;"
        "}"
        ".status-red {"
        "  background-color: #d32f2f;"
        "}"
        ".status-green {"
        "  background-color: #2e7d32;"
        "}";

    GtkCssProvider *provider = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, css);
#else
    gtk_css_provider_load_from_data(provider, css, -1);
#endif
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

static void on_activate(GtkApplication *app, gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    if (state == 0) {
        return;
    }

    install_css();

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Serial Configurator");
    gtk_window_set_default_size(GTK_WINDOW(window), 960, 640);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(root, 16);
    gtk_widget_set_margin_end(root, 16);
    gtk_widget_set_margin_top(root, 16);
    gtk_widget_set_margin_bottom(root, 16);
    gtk_window_set_child(GTK_WINDOW(window), root);

    GtkWidget *top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(root), top_row);

    GtkWidget *detect_button = gtk_button_new_with_label(k_detect_label);
    gtk_box_append(GTK_BOX(top_row), detect_button);
    state->detect_button = detect_button;
    g_signal_connect(detect_button, "clicked", G_CALLBACK(on_detect_clicked), state);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(top_row), spacer);

    GtkWidget *status_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(top_row), status_panel);

    const size_t module_count = sc_core_module_count();
    for (size_t i = 0u; i < module_count; ++i) {
        const ScModuleStatus *status = sc_core_module_status(&state->core, i);
        if (status == 0) {
            continue;
        }

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_append(GTK_BOX(status_panel), row);

        GtkWidget *lamp = gtk_label_new("");
        gtk_widget_set_size_request(lamp, 14, 14);
        gtk_widget_add_css_class(lamp, "status-lamp");
        set_lamp_state(lamp, false);
        state->module_lamps[i] = lamp;

        GtkWidget *name_label = gtk_label_new(status->display_name);
        gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);

        gtk_box_append(GTK_BOX(row), lamp);
        gtk_box_append(GTK_BOX(row), name_label);
    }

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC
    );
    gtk_box_append(GTK_BOX(root), scrolled);

    GtkWidget *log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), log_view);

    state->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    reset_connection_state(state, false);

    gtk_window_present(GTK_WINDOW(window));
}

int sc_app_run(int argc, char *argv[])
{
    AppState state;
    sc_core_init(&state.core);
    state.log_buffer = 0;
    state.detect_button = 0;
    state.connected = false;
    state.detection_in_progress = false;
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        state.module_lamps[i] = 0;
    }

    GtkApplication *app = gtk_application_new(
        "pl.jaszczur.fiesta.serialconfigurator",
        (GApplicationFlags)0
    );

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &state);

    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
