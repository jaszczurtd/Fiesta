#include "sc_module_details.h"

#include <stdio.h>
#include <string.h>

static const char *value_or_dash(const char *value)
{
    return (value != 0 && value[0] != '\0') ? value : "-";
}

static GtkWidget *build_field_row(GtkWidget *parent, int row, const char *label_text)
{
    GtkWidget *label = gtk_label_new(label_text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "dim-label");
    gtk_grid_attach(GTK_GRID(parent), label, 0, row, 1, 1);

    GtkWidget *value = gtk_label_new("-");
    gtk_label_set_xalign(GTK_LABEL(value), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(value), TRUE);
    gtk_grid_attach(GTK_GRID(parent), value, 1, row, 1, 1);
    return value;
}

static void set_label_text(GtkWidget *label, const char *text)
{
    if (label == 0) {
        return;
    }

    gtk_label_set_text(GTK_LABEL(label), text != 0 ? text : "-");
}

static const ScIdentityData *prefer_meta_identity(const ScModuleStatus *status)
{
    if (status == 0) {
        return 0;
    }

    if (status->meta_identity.valid) {
        return &status->meta_identity;
    }

    if (status->hello_identity.valid) {
        return &status->hello_identity;
    }

    return 0;
}

void sc_module_details_init(ScModuleDetailsView *view)
{
    if (view == 0) {
        return;
    }

    memset(view, 0, sizeof(*view));

    GtkWidget *frame = gtk_frame_new("Module Details");
    gtk_widget_set_hexpand(frame, TRUE);
    view->root = frame;

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(content, 12);
    gtk_widget_set_margin_end(content, 12);
    gtk_widget_set_margin_top(content, 12);
    gtk_widget_set_margin_bottom(content, 12);
    gtk_frame_set_child(GTK_FRAME(frame), content);

    GtkWidget *top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(content), top_row);

    GtkWidget *selected_label = gtk_label_new("Selected:");
    gtk_label_set_xalign(GTK_LABEL(selected_label), 0.0f);
    gtk_widget_add_css_class(selected_label, "dim-label");
    gtk_box_append(GTK_BOX(top_row), selected_label);

    view->selected_value = gtk_label_new("-");
    gtk_label_set_xalign(GTK_LABEL(view->selected_value), 0.0f);
    gtk_widget_set_hexpand(view->selected_value, TRUE);
    gtk_box_append(GTK_BOX(top_row), view->selected_value);

    GtkWidget *meta_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(content), meta_row);

    GtkWidget *meta_label = gtk_label_new("Meta status:");
    gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
    gtk_widget_add_css_class(meta_label, "dim-label");
    gtk_box_append(GTK_BOX(meta_row), meta_label);

    view->meta_status_value = gtk_label_new("No metadata request yet.");
    gtk_label_set_xalign(GTK_LABEL(view->meta_status_value), 0.0f);
    gtk_widget_set_hexpand(view->meta_status_value, TRUE);
    gtk_box_append(GTK_BOX(meta_row), view->meta_status_value);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_box_append(GTK_BOX(content), grid);

    view->module_value = build_field_row(grid, 0, "Module");
    view->port_value = build_field_row(grid, 1, "Port");
    view->uid_value = build_field_row(grid, 2, "UID");
    view->fw_value = build_field_row(grid, 3, "FW");
    view->build_value = build_field_row(grid, 4, "Build");
    view->proto_value = build_field_row(grid, 5, "Proto");
    view->session_value = build_field_row(grid, 6, "Session");
}

GtkWidget *sc_module_details_root(const ScModuleDetailsView *view)
{
    if (view == 0) {
        return 0;
    }

    return view->root;
}

void sc_module_details_show_placeholder(ScModuleDetailsView *view, const char *message)
{
    if (view == 0) {
        return;
    }

    set_label_text(view->selected_value, "-");
    set_label_text(view->meta_status_value, value_or_dash(message));
    set_label_text(view->module_value, "-");
    set_label_text(view->port_value, "-");
    set_label_text(view->uid_value, "-");
    set_label_text(view->fw_value, "-");
    set_label_text(view->build_value, "-");
    set_label_text(view->proto_value, "-");
    set_label_text(view->session_value, "-");
}

void sc_module_details_show_module(
    ScModuleDetailsView *view,
    const ScModuleStatus *status,
    const char *meta_status
)
{
    if (view == 0 || status == 0) {
        return;
    }

    set_label_text(view->selected_value, status->display_name);
    set_label_text(view->meta_status_value, value_or_dash(meta_status));

    const ScIdentityData *identity = prefer_meta_identity(status);
    if (identity == 0) {
        set_label_text(view->module_value, value_or_dash(status->display_name));
        set_label_text(view->port_value, status->detected ? value_or_dash(status->port_path) : "-");
        set_label_text(view->uid_value, "-");
        set_label_text(view->fw_value, "-");
        set_label_text(view->build_value, "-");
        set_label_text(view->proto_value, "-");
        set_label_text(view->session_value, "-");
        return;
    }

    char proto_text[32];
    char session_text[32];
    proto_text[0] = '\0';
    session_text[0] = '\0';

    if (identity->proto_present) {
        (void)snprintf(proto_text, sizeof(proto_text), "%d", identity->proto_version);
    } else {
        (void)snprintf(proto_text, sizeof(proto_text), "-");
    }

    if (identity->session_present) {
        (void)snprintf(session_text, sizeof(session_text), "%u", identity->session_id);
    } else {
        (void)snprintf(session_text, sizeof(session_text), "-");
    }

    set_label_text(
        view->module_value,
        value_or_dash(
            identity->module_name[0] != '\0' ? identity->module_name : status->display_name
        )
    );
    set_label_text(view->port_value, status->detected ? value_or_dash(status->port_path) : "-");
    set_label_text(view->uid_value, value_or_dash(identity->uid));
    set_label_text(view->fw_value, value_or_dash(identity->fw_version));
    set_label_text(view->build_value, value_or_dash(identity->build_id));
    set_label_text(view->proto_value, proto_text);
    set_label_text(view->session_value, session_text);
}
