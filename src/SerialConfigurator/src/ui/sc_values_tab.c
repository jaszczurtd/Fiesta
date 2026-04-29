#include "sc_values_tab.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc_core.h"
#include "sc_i18n.h"
#include "sc_protocol.h"

/* ── Per-row + per-subtab state, attached to widgets via
 *    g_object_set_data_full so destruction is automatic. ──────────── */

typedef struct ParamRow {
    char id[SC_PARAM_ID_MAX];
    int16_t applied_value;        /* last value confirmed in firmware staging */
    GtkAdjustment *adjustment;
    GtkWidget *status_label;
    bool dirty;
    bool suppress_change_signal;
} ParamRow;

typedef struct ModuleSubtab {
    AppState *state;
    size_t module_index;
    GPtrArray *rows;              /* of ParamRow* (owns) */
    GtkWidget *footer_status;
} ModuleSubtab;

static void param_row_free(gpointer data)
{
    g_free(data);
}

static void module_subtab_free(gpointer data)
{
    ModuleSubtab *m = (ModuleSubtab *)data;
    if (m == NULL) {
        return;
    }
    if (m->rows != NULL) {
        g_ptr_array_unref(m->rows);
    }
    g_free(m);
}

/* ── Helpers ─────────────────────────────────────────────────────── */

/* Render "cooling_fan" -> "Cooling Fan" for the section header.
 * Pure presentation - the wire token stays snake_case. */
static void format_group_label(const char *token, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0u) {
        return;
    }
    out[0] = '\0';
    if (token == NULL || token[0] == '\0') {
        snprintf(out, out_size, "—");
        return;
    }
    size_t i = 0u;
    bool start_of_word = true;
    while (token[i] != '\0' && i + 1u < out_size) {
        const char c = token[i];
        if (c == '_') {
            out[i] = ' ';
            start_of_word = true;
        } else {
            out[i] = start_of_word ? (char)g_ascii_toupper(c) : c;
            start_of_word = false;
        }
        ++i;
    }
    out[i] = '\0';
}

static int16_t typed_value_to_i16(const ScTypedValue *tv)
{
    if (tv == NULL) {
        return 0;
    }
    switch (tv->type) {
    case SC_VALUE_TYPE_INT:
        if (tv->int_value > INT16_MAX) {
            return INT16_MAX;
        }
        if (tv->int_value < INT16_MIN) {
            return INT16_MIN;
        }
        return (int16_t)tv->int_value;
    case SC_VALUE_TYPE_UINT:
        return (tv->uint_value > (uint64_t)INT16_MAX) ? INT16_MAX
                                                      : (int16_t)tv->uint_value;
    default:
        return 0;
    }
}

static const ScModuleStatus *module_status_for(AppState *state, size_t idx)
{
    if (state == NULL) {
        return NULL;
    }
    return sc_core_module_status(&state->core, idx);
}

static void footer_set_status(ModuleSubtab *m, const char *text)
{
    if (m == NULL || m->footer_status == NULL) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(m->footer_status),
                       (text != NULL) ? text : "");
}

/* ── Row construction + value-changed handler ────────────────────── */

static void on_value_changed(GtkAdjustment *adj, gpointer user_data)
{
    ParamRow *row = (ParamRow *)user_data;
    if (row == NULL || row->suppress_change_signal) {
        return;
    }
    const int16_t cur = (int16_t)gtk_adjustment_get_value(adj);
    row->dirty = (cur != row->applied_value);
    if (row->status_label != NULL) {
        if (row->dirty) {
            char buf[64];
            snprintf(buf, sizeof(buf), "edited (was %d)",
                     (int)row->applied_value);
            gtk_label_set_text(GTK_LABEL(row->status_label), buf);
        } else {
            gtk_label_set_text(GTK_LABEL(row->status_label), "");
        }
    }
}

static GtkWidget *build_param_row(ModuleSubtab *m,
                                  const ScParamDetailData *detail)
{
    ParamRow *row = g_new0(ParamRow, 1);
    snprintf(row->id, sizeof(row->id), "%s", detail->id);
    row->applied_value = typed_value_to_i16(&detail->value);

    /* Spin/scale share one adjustment for tandem-sync without manual
     * binding code. The descriptor's [min, max] becomes the adjustment
     * range so the widget itself enforces clamping; the firmware
     * re-enforces the same bounds at SET_PARAM time. */
    const int16_t min_v = detail->has_min
        ? typed_value_to_i16(&detail->min) : INT16_MIN;
    const int16_t max_v = detail->has_max
        ? typed_value_to_i16(&detail->max) : INT16_MAX;
    const int16_t cur_v = row->applied_value;

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(hbox, 2);
    gtk_widget_set_margin_bottom(hbox, 2);

    GtkWidget *id_label = gtk_label_new(detail->id);
    gtk_label_set_xalign(GTK_LABEL(id_label), 0.0f);
    gtk_widget_set_size_request(id_label, 220, -1);
    gtk_box_append(GTK_BOX(hbox), id_label);

    GtkAdjustment *adj = gtk_adjustment_new((double)cur_v,
                                            (double)min_v, (double)max_v,
                                            1.0, 10.0, 0.0);
    row->adjustment = adj;

    GtkWidget *spin = gtk_spin_button_new(adj, 1.0, 0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin),
                                      GTK_UPDATE_IF_VALID);
    gtk_widget_set_size_request(spin, 100, -1);
    gtk_box_append(GTK_BOX(hbox), spin);

    GtkWidget *scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);
    gtk_box_append(GTK_BOX(hbox), scale);

    GtkWidget *status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
    gtk_widget_set_size_request(status, 220, -1);
    gtk_box_append(GTK_BOX(hbox), status);
    row->status_label = status;

    g_signal_connect(adj, "value-changed",
                     G_CALLBACK(on_value_changed), row);

    g_ptr_array_add(m->rows, row);
    return hbox;
}

/* ── Footer actions: re-auth then SET / COMMIT / REVERT ──────────── */

static bool authenticate_for_subtab(ModuleSubtab *m,
                                    char *err, size_t err_size)
{
    const ScModuleStatus *st = module_status_for(m->state, m->module_index);
    if (st == NULL || !st->detected || st->port_path[0] == '\0') {
        snprintf(err, err_size, "module not detected");
        return false;
    }
    const ScAuthStatus rc = sc_core_authenticate(&m->state->core.transport,
                                                 st->port_path,
                                                 err, err_size);
    return (rc == SC_AUTH_OK);
}

static void on_apply_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ModuleSubtab *m = (ModuleSubtab *)user_data;
    if (m == NULL) {
        return;
    }

    size_t dirty_count = 0u;
    for (size_t i = 0u; i < m->rows->len; ++i) {
        const ParamRow *row = (const ParamRow *)g_ptr_array_index(m->rows, i);
        if (row->dirty) {
            dirty_count++;
        }
    }
    if (dirty_count == 0u) {
        footer_set_status(m,
            sc_i18n_string_get(SC_I18N_VALUES_NOTHING_TO_APPLY));
        return;
    }

    char err[512] = {0};
    if (!authenticate_for_subtab(m, err, sizeof(err))) {
        char buf[640];
        snprintf(buf, sizeof(buf),
                 sc_i18n_string_get(SC_I18N_VALUES_AUTH_FAILED_FMT), err);
        footer_set_status(m, buf);
        return;
    }

    const ScModuleStatus *st = module_status_for(m->state, m->module_index);
    size_t applied = 0u;
    for (size_t i = 0u; i < m->rows->len; ++i) {
        ParamRow *row = (ParamRow *)g_ptr_array_index(m->rows, i);
        if (!row->dirty) {
            continue;
        }
        const int16_t v = (int16_t)gtk_adjustment_get_value(row->adjustment);
        char rerr[512] = {0};
        const ScSetParamStatus rc = sc_core_set_param(
            &m->state->core.transport, st->port_path,
            row->id, v, rerr, sizeof(rerr));
        if (rc == SC_SET_PARAM_OK) {
            row->applied_value = v;
            row->dirty = false;
            if (row->status_label != NULL) {
                gtk_label_set_text(GTK_LABEL(row->status_label), "staged");
            }
            applied++;
            continue;
        }
        if (row->status_label != NULL) {
            gtk_label_set_text(GTK_LABEL(row->status_label),
                               sc_set_param_status_name(rc));
        }
        char fbuf[700];
        snprintf(fbuf, sizeof(fbuf),
                 sc_i18n_string_get(SC_I18N_VALUES_APPLY_FAILED_FMT),
                 row->id, rerr);
        footer_set_status(m, fbuf);
        return;
    }

    char ok_buf[200];
    snprintf(ok_buf, sizeof(ok_buf),
             sc_i18n_string_get(SC_I18N_VALUES_APPLY_OK_FMT),
             (unsigned)applied);
    footer_set_status(m, ok_buf);
}

static void on_commit_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ModuleSubtab *m = (ModuleSubtab *)user_data;
    if (m == NULL) {
        return;
    }
    char err[512] = {0};
    if (!authenticate_for_subtab(m, err, sizeof(err))) {
        char buf[640];
        snprintf(buf, sizeof(buf),
                 sc_i18n_string_get(SC_I18N_VALUES_AUTH_FAILED_FMT), err);
        footer_set_status(m, buf);
        return;
    }
    const ScModuleStatus *st = module_status_for(m->state, m->module_index);
    err[0] = '\0';
    const ScCommitParamsStatus rc = sc_core_commit_params(
        &m->state->core.transport, st->port_path, err, sizeof(err));
    if (rc == SC_COMMIT_PARAMS_OK) {
        for (size_t i = 0u; i < m->rows->len; ++i) {
            ParamRow *row = (ParamRow *)g_ptr_array_index(m->rows, i);
            if (row->status_label != NULL) {
                gtk_label_set_text(GTK_LABEL(row->status_label), "");
            }
        }
        footer_set_status(m, sc_i18n_string_get(SC_I18N_VALUES_COMMIT_OK));
        return;
    }
    char buf[640];
    snprintf(buf, sizeof(buf),
             sc_i18n_string_get(SC_I18N_VALUES_COMMIT_FAILED_FMT), err);
    footer_set_status(m, buf);
}

static void on_revert_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ModuleSubtab *m = (ModuleSubtab *)user_data;
    if (m == NULL) {
        return;
    }
    char err[512] = {0};
    if (!authenticate_for_subtab(m, err, sizeof(err))) {
        char buf[640];
        snprintf(buf, sizeof(buf),
                 sc_i18n_string_get(SC_I18N_VALUES_AUTH_FAILED_FMT), err);
        footer_set_status(m, buf);
        return;
    }
    const ScModuleStatus *st = module_status_for(m->state, m->module_index);
    err[0] = '\0';
    const ScRevertParamsStatus rc = sc_core_revert_params(
        &m->state->core.transport, st->port_path, err, sizeof(err));
    if (rc == SC_REVERT_PARAMS_OK) {
        /* Snap every spinner back to its applied value and clear
         * dirty markers; the firmware-side staging mirror is now
         * back to the active mirror. */
        for (size_t i = 0u; i < m->rows->len; ++i) {
            ParamRow *row = (ParamRow *)g_ptr_array_index(m->rows, i);
            row->suppress_change_signal = true;
            gtk_adjustment_set_value(row->adjustment,
                                     (double)row->applied_value);
            row->suppress_change_signal = false;
            row->dirty = false;
            if (row->status_label != NULL) {
                gtk_label_set_text(GTK_LABEL(row->status_label), "");
            }
        }
        footer_set_status(m, sc_i18n_string_get(SC_I18N_VALUES_REVERT_OK));
        return;
    }
    char buf[640];
    snprintf(buf, sizeof(buf),
             sc_i18n_string_get(SC_I18N_VALUES_REVERT_FAILED_FMT), err);
    footer_set_status(m, buf);
}

/* ── Build a per-module sub-tab ──────────────────────────────────── */

static GtkWidget *build_module_subtab(AppState *state, size_t module_index)
{
    ModuleSubtab *m = g_new0(ModuleSubtab, 1);
    m->state = state;
    m->module_index = module_index;
    m->rows = g_ptr_array_new_with_free_func(param_row_free);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(outer, 12);
    gtk_widget_set_margin_end(outer, 12);
    gtk_widget_set_margin_top(outer, 12);
    gtk_widget_set_margin_bottom(outer, 12);

    /* Tying the lifetime of the ModuleSubtab struct (and its rows
     * GPtrArray) to the outer box lets us forget about manual cleanup
     * when the Values tab is rebuilt. */
    g_object_set_data_full(G_OBJECT(outer), "sc-values-subtab",
                           m, module_subtab_free);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_box_append(GTK_BOX(outer), scroll);

    GtkWidget *form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(form, 4);
    gtk_widget_set_margin_end(form, 4);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), form);

    /* Pull the param list and per-id details synchronously. Each
     * SC_GET_PARAM is one round-trip; ECU's six params + framing
     * overhead total <300 ms over USB CDC. Acceptable for the
     * post-detection rebuild path; if a future module ships a much
     * larger catalog we can move this off the main thread. */
    ScCommandResult result;
    char cmd_log[256];
    const ScModuleStatus *target_status =
        sc_core_module_status(&state->core, module_index);
    const char *target_name = (target_status != NULL && target_status->display_name != NULL)
        ? target_status->display_name : "?";

    if (!sc_core_sc_get_param_list(&state->core, module_index,
                                   &result, cmd_log, sizeof(cmd_log))) {
        char buf[300];
        snprintf(buf, sizeof(buf),
                 sc_i18n_string_get(SC_I18N_VALUES_LOAD_FAILED_FMT),
                 target_name, "transport");
        gtk_box_append(GTK_BOX(form), gtk_label_new(buf));
        return outer;
    }

    ScParamListData list;
    char parse_err[256];
    if (!sc_core_parse_param_list_result(&result, &list,
                                         parse_err, sizeof(parse_err))) {
        char buf[300];
        snprintf(buf, sizeof(buf),
                 sc_i18n_string_get(SC_I18N_VALUES_LOAD_FAILED_FMT),
                 target_name, parse_err);
        gtk_box_append(GTK_BOX(form), gtk_label_new(buf));
        return outer;
    }

    if (list.count == 0u) {
        gtk_box_append(GTK_BOX(form),
            gtk_label_new(sc_i18n_string_get(SC_I18N_VALUES_NO_PARAMS)));
        /* Footer still rendered below so the operator can REVERT
         * across the whole module if a previous session left the
         * firmware-side staging mirror dirty. */
    }

    /* Group bookkeeping: track section box per group token in
     * first-encounter order. SC_PARAM_ITEMS_MAX caps both the param
     * count and the section count - in practice a module has far
     * fewer groups than params, so this is plenty. */
    char seen_groups[SC_PARAM_ITEMS_MAX][SC_PARAM_GROUP_MAX];
    GtkWidget *section_boxes[SC_PARAM_ITEMS_MAX];
    size_t seen_count = 0u;

    for (size_t i = 0u; i < list.count; ++i) {
        ScCommandResult pr;
        char pl[256];
        if (!sc_core_sc_get_param(&state->core, module_index, list.ids[i],
                                  &pr, pl, sizeof(pl))) {
            continue;
        }
        ScParamDetailData detail;
        char err2[256];
        if (!sc_core_parse_param_result(&pr, &detail, err2, sizeof(err2))) {
            continue;
        }
        const char *group = detail.group[0] != '\0' ? detail.group : "general";

        size_t section_idx = seen_count;
        bool seen = false;
        for (size_t j = 0u; j < seen_count; ++j) {
            if (strcmp(seen_groups[j], group) == 0) {
                section_idx = j;
                seen = true;
                break;
            }
        }
        if (!seen) {
            if (seen_count + 1u > SC_PARAM_ITEMS_MAX) {
                continue;
            }
            snprintf(seen_groups[seen_count], SC_PARAM_GROUP_MAX, "%s", group);

            GtkWidget *section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_widget_set_margin_top(section, 4);
            gtk_box_append(GTK_BOX(form), section);

            char label_text[64];
            format_group_label(group, label_text, sizeof(label_text));
            char markup[200];
            snprintf(markup, sizeof(markup),
                     "<b><big>%s</big></b>", label_text);
            GtkWidget *header = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(header), markup);
            gtk_label_set_xalign(GTK_LABEL(header), 0.0f);
            gtk_box_append(GTK_BOX(section), header);

            GtkWidget *separator =
                gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
            gtk_box_append(GTK_BOX(section), separator);

            section_boxes[seen_count] = section;
            section_idx = seen_count;
            seen_count++;
        }

        GtkWidget *row_widget = build_param_row(m, &detail);
        gtk_box_append(GTK_BOX(section_boxes[section_idx]), row_widget);
    }

    /* Footer with [Apply staged] [Commit] [Revert] + status label. */
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(footer, 8);

    GtkWidget *apply = gtk_button_new_with_label(
        sc_i18n_string_get(SC_I18N_VALUES_BTN_APPLY_STAGED));
    GtkWidget *commit = gtk_button_new_with_label(
        sc_i18n_string_get(SC_I18N_VALUES_BTN_COMMIT));
    GtkWidget *revert = gtk_button_new_with_label(
        sc_i18n_string_get(SC_I18N_VALUES_BTN_REVERT));

    g_signal_connect(apply, "clicked", G_CALLBACK(on_apply_clicked), m);
    g_signal_connect(commit, "clicked", G_CALLBACK(on_commit_clicked), m);
    g_signal_connect(revert, "clicked", G_CALLBACK(on_revert_clicked), m);

    gtk_box_append(GTK_BOX(footer), apply);
    gtk_box_append(GTK_BOX(footer), commit);
    gtk_box_append(GTK_BOX(footer), revert);

    GtkWidget *footer_status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(footer_status), 0.0f);
    gtk_widget_set_hexpand(footer_status, TRUE);
    gtk_box_append(GTK_BOX(footer), footer_status);
    m->footer_status = footer_status;

    gtk_box_append(GTK_BOX(outer), footer);
    return outer;
}

/* ── Public API ──────────────────────────────────────────────────── */

GtkWidget *sc_values_tab_build(AppState *state)
{
    if (state == NULL) {
        return NULL;
    }

    /* Outer is a vertical box that holds either the placeholder
     * (pre-detection state) or the inner per-module notebook. We
     * replace the child wholesale on every rebuild rather than mutate
     * the notebook in place - far simpler than tracking which sub-tab
     * came from which module across detection cycles. */
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(outer, 16);
    gtk_widget_set_margin_end(outer, 16);
    gtk_widget_set_margin_top(outer, 16);
    gtk_widget_set_margin_bottom(outer, 16);

    GtkWidget *placeholder = gtk_label_new(
        sc_i18n_string_get(SC_I18N_VALUES_PLACEHOLDER));
    gtk_widget_set_vexpand(placeholder, TRUE);
    gtk_box_append(GTK_BOX(outer), placeholder);

    state->values_tab_root = outer;
    return outer;
}

static void clear_box_children(GtkWidget *box)
{
    GtkWidget *child = gtk_widget_get_first_child(box);
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(box), child);
        child = next;
    }
}

void sc_values_tab_rebuild(AppState *state, bool detected_any)
{
    if (state == NULL || state->values_tab_root == NULL) {
        return;
    }
    GtkWidget *outer = state->values_tab_root;
    clear_box_children(outer);

    if (!detected_any) {
        GtkWidget *placeholder = gtk_label_new(
            sc_i18n_string_get(SC_I18N_VALUES_PLACEHOLDER));
        gtk_widget_set_vexpand(placeholder, TRUE);
        gtk_box_append(GTK_BOX(outer), placeholder);
        return;
    }

    /* Count detected modules first so we can decide between notebook
     * vs "no modules" placeholder without leaving a half-built notebook
     * widget tree. */
    size_t detected = 0u;
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        const ScModuleStatus *st = sc_core_module_status(&state->core, i);
        if (st != NULL && st->detected && !st->target_ambiguous) {
            detected++;
        }
    }
    if (detected == 0u) {
        GtkWidget *empty = gtk_label_new(
            sc_i18n_string_get(SC_I18N_VALUES_NO_DETECTED_MODULES));
        gtk_widget_set_vexpand(empty, TRUE);
        gtk_box_append(GTK_BOX(outer), empty);
        return;
    }

    GtkWidget *notebook = gtk_notebook_new();
    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_widget_set_hexpand(notebook, TRUE);
    gtk_box_append(GTK_BOX(outer), notebook);

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        const ScModuleStatus *st = sc_core_module_status(&state->core, i);
        if (st == NULL || !st->detected || st->target_ambiguous) {
            continue;
        }
        GtkWidget *page = build_module_subtab(state, i);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page,
                                 gtk_label_new(st->display_name));
    }
}
