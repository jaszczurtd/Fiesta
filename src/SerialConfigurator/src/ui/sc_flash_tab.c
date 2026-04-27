#include "sc_flash_tab.h"

#include "sc_i18n.h"

static bool any_in_scope_module_detected(const AppState *state)
{
    if (state == 0) {
        return false;
    }
    const size_t module_count = sc_core_module_count();
    for (size_t i = 0u; i < module_count; ++i) {
        const ScModuleStatus *status = sc_core_module_status(&state->core, i);
        if (status != 0 && status->detected) {
            return true;
        }
    }
    return false;
}

void sc_flash_tab_refresh_sensitivity(AppState *state)
{
    if (state == 0 || state->flash_tab_root == 0) {
        return;
    }
    gtk_widget_set_sensitive(
        state->flash_tab_root,
        any_in_scope_module_detected(state)
    );
}

GtkWidget *sc_flash_tab_build(AppState *state)
{
    if (state == 0) {
        return 0;
    }

    GtkWidget *flash_tab_root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(flash_tab_root, 16);
    gtk_widget_set_margin_end(flash_tab_root, 16);
    gtk_widget_set_margin_top(flash_tab_root, 16);
    gtk_widget_set_margin_bottom(flash_tab_root, 16);

    GtkWidget *flash_placeholder = gtk_label_new(
        sc_i18n_string_get(SC_I18N_FLASH_PLACEHOLDER)
    );
    gtk_label_set_wrap(GTK_LABEL(flash_placeholder), TRUE);
    gtk_label_set_xalign(GTK_LABEL(flash_placeholder), 0.0f);
    gtk_widget_set_valign(flash_placeholder, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(flash_tab_root), flash_placeholder);

    state->flash_tab_root = flash_tab_root;
    sc_flash_tab_refresh_sensitivity(state);
    return flash_tab_root;
}
