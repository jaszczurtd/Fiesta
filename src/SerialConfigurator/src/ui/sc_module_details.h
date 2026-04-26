#ifndef SC_MODULE_DETAILS_H
#define SC_MODULE_DETAILS_H

#include <gtk/gtk.h>

#include "sc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ScModuleDetailsView {
    GtkWidget *root;
    GtkWidget *selected_value;
    GtkWidget *meta_status_value;
    GtkWidget *module_value;
    GtkWidget *port_value;
    GtkWidget *uid_value;
    GtkWidget *fw_value;
    GtkWidget *build_value;
    GtkWidget *proto_value;
    GtkWidget *session_value;
} ScModuleDetailsView;

void sc_module_details_init(ScModuleDetailsView *view);
GtkWidget *sc_module_details_root(const ScModuleDetailsView *view);
void sc_module_details_show_placeholder(ScModuleDetailsView *view, const char *message);
void sc_module_details_show_module(
    ScModuleDetailsView *view,
    const ScModuleStatus *status,
    const char *meta_status
);

#ifdef __cplusplus
}
#endif

#endif /* SC_MODULE_DETAILS_H */
