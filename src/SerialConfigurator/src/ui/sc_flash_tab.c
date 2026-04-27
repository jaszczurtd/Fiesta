#include "sc_flash_tab.h"

#include <stdio.h>
#include <string.h>

#include "sc_flash.h"
#include "sc_flash_paths.h"
#include "sc_i18n.h"
#include "sc_manifest.h"
#include "sc_modules_view.h"   /* refresh_lamps -> calls our refresh_sensitivity */

/*
 * Phase 6.2: per-module Flash sections.
 *
 * The body widget at @p state->flash_tab_root is a vertical box. On
 * every detection cycle it is rebuilt from scratch by
 * sc_flash_tab_rebuild_sections — each in-scope module that is
 * currently detected gets a GtkFrame section with:
 *   - UF2 file picker (Choose / Clear) + path label
 *   - manifest file picker (optional, Choose / Clear) + path label
 *   - read-only status label (selectable text)
 *   - GtkProgressBar (idle-hidden until 6.5 wires flashing)
 *   - Flash button (stubbed in 6.2 — exercises the global lock)
 *
 * Per-section widget pointers live in a private static array so the
 * AppState struct does not have to grow N pointers per module. A
 * back-pointer to AppState is embedded in each section so callbacks
 * can update persisted paths and the global lock.
 *
 * The lock policy required by §9 Phase 6.2:
 *   while a flash is running, every other Flash button, the Detect
 *   button, and every section's file pickers go insensitive. The
 *   active section's status field stays live.
 * The 6.2 stub only exercises the plumbing — the Flash button
 * enters the lock, prints "TODO Phase 6.5" into the status field,
 * and a g_timeout_add releases the lock 1.5 s later.
 */

typedef struct ScFlashSection {
    bool active;
    AppState *state;
    size_t module_index;
    char module_name[32];

    GtkWidget *frame;
    GtkWidget *uf2_label;
    GtkWidget *manifest_label;
    GtkWidget *uf2_pick_btn;
    GtkWidget *uf2_clear_btn;
    GtkWidget *manifest_pick_btn;
    GtkWidget *manifest_clear_btn;
    GtkWidget *flash_btn;
    GtkWidget *status_label;
    GtkWidget *progress_bar;
} ScFlashSection;

static ScFlashSection s_sections[SC_MODULE_COUNT];

/* ── helpers ───────────────────────────────────────────────────────── */

static const char *display_or_none(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return sc_i18n_string_get(SC_I18N_FLASH_NO_PATH);
    }
    return path;
}

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

static void clear_container(GtkWidget *box)
{
    if (box == NULL) return;
    GtkWidget *child = gtk_widget_get_first_child(box);
    while (child != NULL) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(box), child);
        child = next;
    }
}

static void section_set_status(ScFlashSection *s, const char *text)
{
    if (s == NULL || s->status_label == NULL || text == NULL) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(s->status_label), text);
}

/* ── format-check / manifest-verify drivers ────────────────────────── */

static void run_uf2_format_check(ScFlashSection *s, const char *path)
{
    if (s == NULL || path == NULL || path[0] == '\0') {
        section_set_status(s, sc_i18n_string_get(SC_I18N_FLASH_STATUS_INITIAL));
        return;
    }

    char err[256];
    err[0] = '\0';
    const sc_flash_status_t st = sc_flash_uf2_format_check(path, err, sizeof(err));
    char buf[512];
    if (st == SC_FLASH_OK) {
        (void)snprintf(buf, sizeof(buf),
                       sc_i18n_string_get(SC_I18N_FLASH_STATUS_UF2_OK_FMT),
                       err);
    } else {
        (void)snprintf(buf, sizeof(buf),
                       sc_i18n_string_get(SC_I18N_FLASH_STATUS_UF2_FAIL_FMT),
                       sc_flash_status_str(st),
                       err);
    }
    section_set_status(s, buf);
}

static void run_manifest_verify(ScFlashSection *s,
                                const char *manifest_path,
                                const char *uf2_path)
{
    if (s == NULL) return;
    if (manifest_path == NULL || manifest_path[0] == '\0') {
        /* Manifest cleared -> fall back to UF2-only status (or initial). */
        if (uf2_path != NULL && uf2_path[0] != '\0') {
            run_uf2_format_check(s, uf2_path);
        } else {
            section_set_status(s, sc_i18n_string_get(SC_I18N_FLASH_STATUS_INITIAL));
        }
        return;
    }

    sc_manifest_t m;
    const sc_manifest_status_t parse_st = sc_manifest_load_file(manifest_path, &m);
    if (parse_st != SC_MANIFEST_OK) {
        char buf[512];
        (void)snprintf(buf, sizeof(buf),
                       sc_i18n_string_get(SC_I18N_FLASH_STATUS_MANIFEST_PARSE_FAIL_FMT),
                       sc_manifest_status_str(parse_st));
        section_set_status(s, buf);
        return;
    }

    if (uf2_path == NULL || uf2_path[0] == '\0') {
        section_set_status(s,
            sc_i18n_string_get(SC_I18N_FLASH_STATUS_MANIFEST_NEEDS_UF2));
        return;
    }

    const sc_manifest_status_t verify_st =
        sc_manifest_verify_artifact(&m, uf2_path);
    if (verify_st != SC_MANIFEST_OK) {
        char buf[512];
        (void)snprintf(buf, sizeof(buf),
                       sc_i18n_string_get(SC_I18N_FLASH_STATUS_MANIFEST_VERIFY_FAIL_FMT),
                       sc_manifest_status_str(verify_st));
        section_set_status(s, buf);
        return;
    }

    section_set_status(s, sc_i18n_string_get(SC_I18N_FLASH_STATUS_MANIFEST_OK));
}

/* Update the section's status to reflect the current persisted state.
 * Called after picker changes and after rebuilds. */
static void section_recompute_status(ScFlashSection *s)
{
    if (s == NULL || s->state == NULL) return;
    const char *uf2 = sc_flash_paths_get_uf2(&s->state->flash_paths,
                                             s->module_name);
    const char *manifest = sc_flash_paths_get_manifest(&s->state->flash_paths,
                                                       s->module_name);

    if ((uf2 == NULL || uf2[0] == '\0') &&
        (manifest == NULL || manifest[0] == '\0')) {
        section_set_status(s, sc_i18n_string_get(SC_I18N_FLASH_STATUS_INITIAL));
        return;
    }

    if (manifest != NULL && manifest[0] != '\0') {
        run_manifest_verify(s, manifest, uf2);
    } else {
        run_uf2_format_check(s, uf2);
    }
}

/* ── file-dialog plumbing ──────────────────────────────────────────── */

typedef struct DialogCtx {
    ScFlashSection *section;
    bool is_manifest;       /* true: manifest picker; false: UF2 */
} DialogCtx;

#if GTK_CHECK_VERSION(4, 10, 0)
static void on_file_dialog_open_finish(GObject *source,
                                       GAsyncResult *result,
                                       gpointer user_data)
{
    DialogCtx *ctx = (DialogCtx *)user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);

    if (file != NULL) {
        char *path = g_file_get_path(file);
        if (path != NULL && ctx != NULL && ctx->section != NULL) {
            if (ctx->is_manifest) {
                sc_flash_paths_set_manifest(&ctx->section->state->flash_paths,
                                            ctx->section->module_name,
                                            path);
                gtk_label_set_text(GTK_LABEL(ctx->section->manifest_label),
                                   display_or_none(path));
            } else {
                sc_flash_paths_set_uf2(&ctx->section->state->flash_paths,
                                       ctx->section->module_name,
                                       path);
                gtk_label_set_text(GTK_LABEL(ctx->section->uf2_label),
                                   display_or_none(path));
            }
            (void)sc_flash_paths_save(&ctx->section->state->flash_paths);
            section_recompute_status(ctx->section);
        }
        g_free(path);
        g_object_unref(file);
    } else if (error != NULL) {
        /* User-cancelled or genuine error — leave state unchanged. */
        g_clear_error(&error);
    }

    g_free(ctx);
}

static void open_file_dialog(ScFlashSection *section, bool is_manifest)
{
    if (section == NULL) return;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog,
        sc_i18n_string_get(is_manifest
                           ? SC_I18N_FLASH_DIALOG_MANIFEST_TITLE
                           : SC_I18N_FLASH_DIALOG_UF2_TITLE));

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter,
        sc_i18n_string_get(is_manifest
                           ? SC_I18N_FLASH_FILTER_MANIFEST
                           : SC_I18N_FLASH_FILTER_UF2));
    gtk_file_filter_add_pattern(filter, is_manifest ? "*.json" : "*.uf2");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(filters);
    g_object_unref(filter);

    GtkRoot *root = gtk_widget_get_root(section->frame);
    GtkWindow *parent = GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;

    DialogCtx *ctx = g_new0(DialogCtx, 1);
    ctx->section = section;
    ctx->is_manifest = is_manifest;
    gtk_file_dialog_open(dialog, parent, NULL, on_file_dialog_open_finish, ctx);
    g_object_unref(dialog);
}
#else
static void open_file_dialog(ScFlashSection *section, bool is_manifest)
{
    /* GTK <4.10 has no GtkFileDialog. The build target is GTK 4.14
     * (CI), so this branch is dead in practice. Surface it via the
     * status field if someone manages to hit it. */
    (void)is_manifest;
    section_set_status(section,
        "GtkFileDialog requires GTK 4.10+; rebuild on a newer host.");
}
#endif

/* ── button callbacks ──────────────────────────────────────────────── */

static void on_uf2_pick_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    open_file_dialog((ScFlashSection *)user_data, false);
}

static void on_manifest_pick_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    open_file_dialog((ScFlashSection *)user_data, true);
}

static void on_uf2_clear_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ScFlashSection *s = (ScFlashSection *)user_data;
    if (s == NULL || s->state == NULL) return;
    sc_flash_paths_set_uf2(&s->state->flash_paths, s->module_name, "");
    (void)sc_flash_paths_save(&s->state->flash_paths);
    gtk_label_set_text(GTK_LABEL(s->uf2_label),
                       sc_i18n_string_get(SC_I18N_FLASH_NO_PATH));
    section_recompute_status(s);
}

static void on_manifest_clear_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ScFlashSection *s = (ScFlashSection *)user_data;
    if (s == NULL || s->state == NULL) return;
    sc_flash_paths_set_manifest(&s->state->flash_paths, s->module_name, "");
    (void)sc_flash_paths_save(&s->state->flash_paths);
    gtk_label_set_text(GTK_LABEL(s->manifest_label),
                       sc_i18n_string_get(SC_I18N_FLASH_NO_PATH));
    section_recompute_status(s);
}

static gboolean on_lock_release_timeout(gpointer user_data)
{
    AppState *state = (AppState *)user_data;
    if (state == NULL) return G_SOURCE_REMOVE;

    sc_flash_tab_set_lock(state, false);

    /* Show a transient note in every active section's status so the
     * operator sees the lock release. Actual flash flow lands later. */
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        if (s_sections[i].active && s_sections[i].status_label != NULL) {
            section_set_status(&s_sections[i],
                sc_i18n_string_get(SC_I18N_FLASH_STATUS_LOCK_RELEASED));
        }
    }
    return G_SOURCE_REMOVE;
}

static void on_flash_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ScFlashSection *s = (ScFlashSection *)user_data;
    if (s == NULL || s->state == NULL) return;

    sc_flash_tab_set_lock(s->state, true);
    section_set_status(s, sc_i18n_string_get(SC_I18N_FLASH_STATUS_FLASH_TODO));
    /* Phase 6.5 will replace this timeout with the real flash flow. */
    (void)g_timeout_add(1500u, on_lock_release_timeout, s->state);
}

/* ── section construction ──────────────────────────────────────────── */

static void section_init_clean(ScFlashSection *s)
{
    s->active = false;
    s->state = NULL;
    s->module_index = 0u;
    s->module_name[0] = '\0';
    s->frame = NULL;
    s->uf2_label = NULL;
    s->manifest_label = NULL;
    s->uf2_pick_btn = NULL;
    s->uf2_clear_btn = NULL;
    s->manifest_pick_btn = NULL;
    s->manifest_clear_btn = NULL;
    s->flash_btn = NULL;
    s->status_label = NULL;
    s->progress_bar = NULL;
}

static GtkWidget *build_section(AppState *state, size_t module_index)
{
    if (state == NULL || module_index >= SC_MODULE_COUNT) {
        return NULL;
    }
    const ScModuleStatus *status = sc_core_module_status(&state->core, module_index);
    if (status == NULL) {
        return NULL;
    }

    ScFlashSection *s = &s_sections[module_index];
    section_init_clean(s);
    s->active = true;
    s->state = state;
    s->module_index = module_index;
    (void)snprintf(s->module_name, sizeof(s->module_name), "%s",
                   status->display_name);

    char header[96];
    (void)snprintf(header, sizeof(header),
                   sc_i18n_string_get(SC_I18N_FLASH_SECTION_HEADER_FMT),
                   status->display_name);

    s->frame = gtk_frame_new(header);
    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(body, 12);
    gtk_widget_set_margin_end(body, 12);
    gtk_widget_set_margin_top(body, 8);
    gtk_widget_set_margin_bottom(body, 8);
    gtk_frame_set_child(GTK_FRAME(s->frame), body);

    /* UF2 row */
    GtkWidget *uf2_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(body), uf2_row);
    GtkWidget *uf2_lbl = gtk_label_new(sc_i18n_string_get(SC_I18N_FLASH_LBL_UF2));
    gtk_label_set_xalign(GTK_LABEL(uf2_lbl), 0.0f);
    gtk_widget_set_size_request(uf2_lbl, 160, -1);
    gtk_box_append(GTK_BOX(uf2_row), uf2_lbl);

    s->uf2_label = gtk_label_new(display_or_none(
        sc_flash_paths_get_uf2(&state->flash_paths, s->module_name)));
    gtk_label_set_xalign(GTK_LABEL(s->uf2_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(s->uf2_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_selectable(GTK_LABEL(s->uf2_label), TRUE);
    gtk_widget_set_hexpand(s->uf2_label, TRUE);
    gtk_box_append(GTK_BOX(uf2_row), s->uf2_label);

    s->uf2_pick_btn = gtk_button_new_with_label(
        sc_i18n_string_get(SC_I18N_FLASH_BTN_PICK_UF2));
    g_signal_connect(s->uf2_pick_btn, "clicked",
                     G_CALLBACK(on_uf2_pick_clicked), s);
    gtk_box_append(GTK_BOX(uf2_row), s->uf2_pick_btn);

    s->uf2_clear_btn = gtk_button_new_with_label(
        sc_i18n_string_get(SC_I18N_FLASH_BTN_CLEAR_UF2));
    g_signal_connect(s->uf2_clear_btn, "clicked",
                     G_CALLBACK(on_uf2_clear_clicked), s);
    gtk_box_append(GTK_BOX(uf2_row), s->uf2_clear_btn);

    /* Manifest row */
    GtkWidget *m_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(body), m_row);
    GtkWidget *m_lbl = gtk_label_new(sc_i18n_string_get(SC_I18N_FLASH_LBL_MANIFEST));
    gtk_label_set_xalign(GTK_LABEL(m_lbl), 0.0f);
    gtk_widget_set_size_request(m_lbl, 160, -1);
    gtk_box_append(GTK_BOX(m_row), m_lbl);

    s->manifest_label = gtk_label_new(display_or_none(
        sc_flash_paths_get_manifest(&state->flash_paths, s->module_name)));
    gtk_label_set_xalign(GTK_LABEL(s->manifest_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(s->manifest_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_selectable(GTK_LABEL(s->manifest_label), TRUE);
    gtk_widget_set_hexpand(s->manifest_label, TRUE);
    gtk_box_append(GTK_BOX(m_row), s->manifest_label);

    s->manifest_pick_btn = gtk_button_new_with_label(
        sc_i18n_string_get(SC_I18N_FLASH_BTN_PICK_MANIFEST));
    g_signal_connect(s->manifest_pick_btn, "clicked",
                     G_CALLBACK(on_manifest_pick_clicked), s);
    gtk_box_append(GTK_BOX(m_row), s->manifest_pick_btn);

    s->manifest_clear_btn = gtk_button_new_with_label(
        sc_i18n_string_get(SC_I18N_FLASH_BTN_CLEAR_MANIFEST));
    g_signal_connect(s->manifest_clear_btn, "clicked",
                     G_CALLBACK(on_manifest_clear_clicked), s);
    gtk_box_append(GTK_BOX(m_row), s->manifest_clear_btn);

    /* Flash + progress + status */
    GtkWidget *action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(body), action_row);

    s->flash_btn = gtk_button_new_with_label(
        sc_i18n_string_get(SC_I18N_FLASH_BTN_FLASH));
    g_signal_connect(s->flash_btn, "clicked",
                     G_CALLBACK(on_flash_clicked), s);
    gtk_box_append(GTK_BOX(action_row), s->flash_btn);

    s->progress_bar = gtk_progress_bar_new();
    gtk_widget_set_hexpand(s->progress_bar, TRUE);
    gtk_widget_set_visible(s->progress_bar, FALSE);
    gtk_box_append(GTK_BOX(action_row), s->progress_bar);

    s->status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(s->status_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(s->status_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(s->status_label), TRUE);
    gtk_box_append(GTK_BOX(body), s->status_label);

    section_recompute_status(s);
    return s->frame;
}

/* ── public API ────────────────────────────────────────────────────── */

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

void sc_flash_tab_rebuild_sections(AppState *state)
{
    if (state == NULL || state->flash_tab_root == NULL) {
        return;
    }

    /* Drop every existing widget; then either add a placeholder (no
     * detected module) or one section per detected in-scope module.
     * Per-section widget pointers in s_sections are reset to NULL so
     * stale callbacks from a previous rebuild cannot fire. */
    clear_container(state->flash_tab_root);
    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        section_init_clean(&s_sections[i]);
    }

    if (!any_in_scope_module_detected(state)) {
        GtkWidget *placeholder = gtk_label_new(
            sc_i18n_string_get(SC_I18N_FLASH_PLACEHOLDER));
        gtk_label_set_wrap(GTK_LABEL(placeholder), TRUE);
        gtk_label_set_xalign(GTK_LABEL(placeholder), 0.0f);
        gtk_widget_set_valign(placeholder, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(state->flash_tab_root), placeholder);
        return;
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        const ScModuleStatus *status = sc_core_module_status(&state->core, i);
        if (status == NULL || !status->detected) {
            continue;
        }
        GtkWidget *frame = build_section(state, i);
        if (frame != NULL) {
            gtk_box_append(GTK_BOX(state->flash_tab_root), frame);
        }
    }
}

void sc_flash_tab_set_lock(AppState *state, bool locked)
{
    if (state == NULL) return;
    state->flash_in_progress = locked;

    /* Detect button — sc_detection.c owns the runtime state but the
     * widget pointer is in AppState, so we can toggle it directly. */
    if (state->detect_button != NULL) {
        gtk_widget_set_sensitive(state->detect_button, !locked);
    }

    for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
        ScFlashSection *s = &s_sections[i];
        if (!s->active) continue;
        const bool sensitive = !locked;
        if (s->uf2_pick_btn != NULL)         gtk_widget_set_sensitive(s->uf2_pick_btn, sensitive);
        if (s->uf2_clear_btn != NULL)        gtk_widget_set_sensitive(s->uf2_clear_btn, sensitive);
        if (s->manifest_pick_btn != NULL)    gtk_widget_set_sensitive(s->manifest_pick_btn, sensitive);
        if (s->manifest_clear_btn != NULL)   gtk_widget_set_sensitive(s->manifest_clear_btn, sensitive);
        if (s->flash_btn != NULL)            gtk_widget_set_sensitive(s->flash_btn, sensitive);
        /* status_label and progress_bar stay live during a flash. */
    }
}

GtkWidget *sc_flash_tab_build(AppState *state)
{
    if (state == 0) {
        return 0;
    }

    /* Persisted paths first — every freshly-rebuilt section reads
     * from state->flash_paths to populate its labels. A missing or
     * malformed file is treated as "no remembered paths yet". */
    sc_flash_paths_init(&state->flash_paths);
    (void)sc_flash_paths_load(&state->flash_paths);

    GtkWidget *flash_tab_root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(flash_tab_root, 16);
    gtk_widget_set_margin_end(flash_tab_root, 16);
    gtk_widget_set_margin_top(flash_tab_root, 16);
    gtk_widget_set_margin_bottom(flash_tab_root, 16);

    state->flash_tab_root = flash_tab_root;
    state->flash_in_progress = false;

    /* Initial body — placeholder until first detection. */
    sc_flash_tab_rebuild_sections(state);
    sc_flash_tab_refresh_sensitivity(state);
    return flash_tab_root;
}
