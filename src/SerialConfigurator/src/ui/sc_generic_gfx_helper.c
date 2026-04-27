#include "sc_generic_gfx_helper.h"

void sc_gfx_install_css(void)
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
        "}"
        ".dim-label {"
        "  color: #5f6368;"
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

void sc_gfx_set_lamp_state(GtkWidget *lamp, bool detected)
{
    if (lamp == 0) {
        return;
    }

    gtk_widget_remove_css_class(lamp, "status-red");
    gtk_widget_remove_css_class(lamp, "status-green");
    gtk_widget_add_css_class(lamp, detected ? "status-green" : "status-red");
}
