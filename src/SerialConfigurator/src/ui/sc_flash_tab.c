#include "sc_flash_tab.h"

#include <stdio.h>
#include <string.h>

#include "sc_core.h"
#include "sc_detection.h"
#include "sc_flash.h"
#include "sc_flash_paths.h"
#include "sc_i18n.h"
#include "sc_manifest.h"
#include "sc_modules_view.h"   /* refresh_lamps -> calls our refresh_sensitivity */
#include "sc_progressbar.h"
#include "../../common/scDefinitions/sc_fiesta_module_tokens.h"

/*
 * Phase 6.2: per-module Flash sections.
 *
 * The body widget at @p state->flash_tab_root is a vertical box. On
 * every detection cycle it is rebuilt from scratch by
 * sc_flash_tab_rebuild_sections - each in-scope module that is
 * currently detected gets a GtkFrame section with:
 *   - UF2 file picker (Choose / Clear) + path label
 *   - manifest file picker (optional, Choose / Clear) + path label
 *   - read-only status label (selectable text)
 *   - GtkProgressBar (idle-hidden until 6.5 wires flashing)
 *   - Flash button (stubbed in 6.2 - exercises the global lock)
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
 * The 6.2 stub only exercises the plumbing - the Flash button
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
    GtkWidget *progress_area;
    GtkWidget *progress_slot;
    GtkWidget *progress_bar;
    GtkWidget *progress_result_label;
    /* Creep state - advances the determinate progress bar smoothly
     * within a phase's [start, end) range while the orchestrator is
     * blocked on a single-event phase (AUTH, REBOOT, WAIT_BOOTSEL,
     * WAIT_REENUM). Without this the bar jumps to phase_start and
     * sits there for several seconds. COPY drives the bar from real
     * per-chunk events, so creep is paused while in COPY. */
    guint creep_timer_id;
    ScFlashPhase creep_phase;
    double creep_from_fraction;   /* dynamic creep start for smooth ramps */
    int64_t creep_anchor_ms;     /* g_get_monotonic_time of phase entry */
} ScFlashSection;

/* Discrete fraction assigned to each phase of @ref sc_core_flash. The
 * bar jumps to the phase's @c start fraction on phase entry and
 * (during COPY) interpolates linearly between @c start and the next
 * phase's @c start as the per-chunk progress events arrive. Weights
 * roughly mirror real-world wall-clock budgets observed on Mint /
 * Cinnamon: WAIT_BOOTSEL is the longest non-COPY wait (auto-mount
 * race, 1-5 s), COPY itself dominates total throughput, WAIT_REENUM
 * is short. Sums to 1.0 across the table. */
typedef struct {
    ScFlashPhase phase;
    double start;
    /* Expected wall-clock duration in ms used by the creep ticker to
     * pace the bar's movement within a phase. Picked from observed
     * runs on Mint / Cinnamon, biased slightly long so the creep
     * finishes a touch before the actual phase boundary fires. */
    double expected_ms;
} PhaseFraction;

static const PhaseFraction k_phase_fractions[] = {
    { SC_FLASH_PHASE_FORMAT_CHECK,
      SC_UI_FLASH_PHASE_FORMAT_CHECK_START,
      SC_UI_FLASH_PHASE_FORMAT_CHECK_EXPECTED_MS },
    { SC_FLASH_PHASE_MANIFEST_VERIFY,
      SC_UI_FLASH_PHASE_MANIFEST_VERIFY_START,
      SC_UI_FLASH_PHASE_MANIFEST_VERIFY_EXPECTED_MS },
    { SC_FLASH_PHASE_AUTHENTICATE,
      SC_UI_FLASH_PHASE_AUTHENTICATE_START,
      SC_UI_FLASH_PHASE_AUTHENTICATE_EXPECTED_MS },
    { SC_FLASH_PHASE_REBOOT_TO_BOOTLOADER,
      SC_UI_FLASH_PHASE_REBOOT_TO_BOOTLOADER_START,
      SC_UI_FLASH_PHASE_REBOOT_TO_BOOTLOADER_EXPECTED_MS },
    { SC_FLASH_PHASE_WAIT_BOOTSEL,
      SC_UI_FLASH_PHASE_WAIT_BOOTSEL_START,
      SC_UI_FLASH_PHASE_WAIT_BOOTSEL_EXPECTED_MS },
    { SC_FLASH_PHASE_COPY,
      SC_UI_FLASH_PHASE_COPY_START,
      SC_UI_FLASH_PHASE_COPY_EXPECTED_MS },
    { SC_FLASH_PHASE_WAIT_REENUM,
      SC_UI_FLASH_PHASE_WAIT_REENUM_START,
      SC_UI_FLASH_PHASE_WAIT_REENUM_EXPECTED_MS },
    { SC_FLASH_PHASE_POST_FLASH_HELLO,
      SC_UI_FLASH_PHASE_POST_FLASH_HELLO_START,
      SC_UI_FLASH_PHASE_POST_FLASH_HELLO_EXPECTED_MS },
};
static const size_t k_phase_fractions_count =
    sizeof(k_phase_fractions) / sizeof(k_phase_fractions[0]);

static double phase_start_fraction(ScFlashPhase phase)
{
    for (size_t i = 0u; i < k_phase_fractions_count; ++i) {
        if (k_phase_fractions[i].phase == phase) {
            return k_phase_fractions[i].start;
        }
    }
    return 0.0;
}

static double phase_end_fraction(ScFlashPhase phase)
{
    for (size_t i = 0u; i < k_phase_fractions_count; ++i) {
        if (k_phase_fractions[i].phase == phase) {
            if (i + 1u < k_phase_fractions_count) {
                return k_phase_fractions[i + 1u].start;
            }
            return 1.0;
        }
    }
    return 0.0;
}

static double phase_expected_ms(ScFlashPhase phase)
{
    for (size_t i = 0u; i < k_phase_fractions_count; ++i) {
        if (k_phase_fractions[i].phase == phase) {
            return k_phase_fractions[i].expected_ms;
        }
    }
    return 1000.0;
}

static ScFlashSection s_sections[SC_MODULE_COUNT];

typedef enum ScLastFlashResult {
    SC_LAST_FLASH_NONE = 0,
    SC_LAST_FLASH_OK,
    SC_LAST_FLASH_FAIL
} ScLastFlashResult;

/* Persisted across tab rebuilds so a post-flash detect refresh does
 * not immediately erase the operator-facing "OK/BLAD" marker. */
static ScLastFlashResult s_last_flash_result[SC_MODULE_COUNT];

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

static void section_set_progress_visual(ScFlashSection *s, double frac)
{
    if (s == NULL || s->progress_bar == NULL) {
        return;
    }
    sc_progressbar_set_fraction(s->progress_bar, frac);
}

static void section_show_progress_bar(ScFlashSection *s)
{
    if (s == NULL || s->progress_slot == NULL || s->progress_bar == NULL) {
        return;
    }
    if (s->progress_area != NULL) {
        gtk_widget_set_visible(s->progress_area, TRUE);
    }
    gtk_widget_set_visible(s->progress_slot, TRUE);
    gtk_widget_set_visible(s->progress_bar, TRUE);
    if (s->progress_result_label != NULL) {
        gtk_widget_set_visible(s->progress_result_label, FALSE);
    }
}

static void section_show_progress_result(ScFlashSection *s, bool ok)
{
    if (s == NULL || s->progress_slot == NULL || s->progress_result_label == NULL) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(s->progress_result_label),
        sc_i18n_string_get(ok ? SC_I18N_FLASH_RESULT_OK
                              : SC_I18N_FLASH_RESULT_FAIL));
    if (s->progress_area != NULL) {
        gtk_widget_set_visible(s->progress_area, TRUE);
    }
    gtk_widget_set_visible(s->progress_slot, TRUE);
    gtk_widget_set_visible(s->progress_result_label, TRUE);
    if (s->progress_bar != NULL) {
        gtk_widget_set_visible(s->progress_bar, FALSE);
    }
}

/* ── progress-bar phase creep ──────────────────────────────────────── */

/* Tick callback - advances the bar from phase_start toward phase_end
 * based on elapsed time within the phase. Caps at phase_end - epsilon
 * so a long-running phase does not cause the bar to overrun the next
 * phase's start before the next FLASH_EV_PROGRESS event arrives. */
static gboolean on_creep_tick(gpointer user_data)
{
    ScFlashSection *s = (ScFlashSection *)user_data;
    if (s == NULL || s->progress_bar == NULL) {
        return G_SOURCE_REMOVE;
    }
    const double phase_end   = phase_end_fraction(s->creep_phase);
    const double expected_ms = phase_expected_ms(s->creep_phase);
    const int64_t now_us = g_get_monotonic_time();
    const double elapsed_ms =
        (double)(now_us - s->creep_anchor_ms * 1000) / 1000.0;
    /* Linear ramp toward phase_end over expected_ms; capped just
     * below phase_end so the bar never visually "completes" a phase
     * the orchestrator has not yet finished. */
    double t = (expected_ms > 0.0) ? (elapsed_ms / expected_ms) : 1.0;
    if (t > 1.0) t = 1.0;
    const double cap = phase_end - SC_UI_FLASH_CREEP_PHASE_EPSILON;
    double from = s->creep_from_fraction;
    if (from < 0.0) from = 0.0;
    if (from > cap) from = cap;
    double frac = from + (cap - from) * t;
    if (frac < from) frac = from;
    if (frac > cap) frac = cap;
    section_set_progress_visual(s, frac);
    return G_SOURCE_CONTINUE;
}

static void start_phase_creep(ScFlashSection *s, ScFlashPhase phase,
                              double from_fraction)
{
    if (s == NULL) return;
    /* Anchor the timer to "now" and remember the phase so the tick
     * computes the right ramp. If a creep was already running, just
     * reset its anchor - same source id keeps firing. */
    s->creep_phase = phase;
    if (from_fraction < 0.0) from_fraction = 0.0;
    if (from_fraction > 1.0) from_fraction = 1.0;
    s->creep_from_fraction = from_fraction;
    s->creep_anchor_ms = g_get_monotonic_time() / 1000;
    if (s->creep_timer_id != 0u) {
        return;
    }
    /* 100 ms tick gives ~10 fps, enough for smooth motion without
     * burning CPU on idle redraws. */
    s->creep_timer_id = g_timeout_add(SC_UI_FLASH_CREEP_TICK_MS, on_creep_tick, s);
}

static void stop_phase_creep(ScFlashSection *s)
{
    if (s == NULL || s->creep_timer_id == 0u) {
        return;
    }
    g_source_remove(s->creep_timer_id);
    s->creep_timer_id = 0u;
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

#if GTK_CHECK_VERSION(4, 10, 0)

typedef struct DialogCtx {
    ScFlashSection *section;
    bool is_manifest;       /* true: manifest picker; false: UF2 */
} DialogCtx;

static GFile *dialog_initial_folder_from_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }

    char *dir = g_path_get_dirname(path);
    if (dir == NULL) {
        return NULL;
    }

    GFile *folder = NULL;
    if (g_file_test(dir, G_FILE_TEST_IS_DIR)) {
        folder = g_file_new_for_path(dir);
    }
    g_free(dir);
    return folder;
}

static const char *module_source_dir_name(const char *module_display_name)
{
    if (module_display_name == NULL) {
        return NULL;
    }
    if (strcmp(module_display_name, SC_MODULE_ECU) == 0) {
        return SC_MODULE_ECU;
    }
    if (strcmp(module_display_name, SC_MODULE_CLOCKS) == 0) {
        return SC_MODULE_CLOCKS;
    }
    if (strcmp(module_display_name, SC_MODULE_OIL_AND_SPEED) == 0) {
        return SC_MODULE_OIL_AND_SPEED;
    }
    return NULL;
}

static GFile *dialog_initial_folder_from_module_build(const ScFlashSection *section)
{
    if (section == NULL) {
        return NULL;
    }

    const char *module_dir = module_source_dir_name(section->module_name);
    if (module_dir == NULL) {
        return NULL;
    }

    static const char *const k_roots[] = {
        ".", "..", "../..", "../../..", "../../../..",
    };
    const size_t root_count = sizeof(k_roots) / sizeof(k_roots[0]);

    for (size_t i = 0u; i < root_count; ++i) {
        char *candidate = g_build_filename(
            k_roots[i], module_dir, ".build", NULL);
        if (candidate != NULL && g_file_test(candidate, G_FILE_TEST_IS_DIR)) {
            GFile *folder = g_file_new_for_path(candidate);
            g_free(candidate);
            return folder;
        }
        g_free(candidate);
    }
    return NULL;
}

static GFile *dialog_initial_folder_for_picker(const ScFlashSection *section,
                                               bool is_manifest)
{
    if (section == NULL || section->state == NULL) {
        return NULL;
    }

    const char *primary = is_manifest
        ? sc_flash_paths_get_manifest(&section->state->flash_paths, section->module_name)
        : sc_flash_paths_get_uf2(&section->state->flash_paths, section->module_name);
    GFile *folder = dialog_initial_folder_from_path(primary);
    if (folder != NULL) {
        return folder;
    }

    const char *secondary = is_manifest
        ? sc_flash_paths_get_uf2(&section->state->flash_paths, section->module_name)
        : sc_flash_paths_get_manifest(&section->state->flash_paths, section->module_name);
    folder = dialog_initial_folder_from_path(secondary);
    if (folder != NULL) {
        return folder;
    }

    return dialog_initial_folder_from_module_build(section);
}

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
        /* User-cancelled or genuine error - leave state unchanged. */
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

    GFile *initial_folder = dialog_initial_folder_for_picker(section, is_manifest);
    if (initial_folder != NULL) {
        gtk_file_dialog_set_initial_folder(dialog, initial_folder);
        g_object_unref(initial_folder);
    }

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

/* ── Phase 6.5 worker plumbing ─────────────────────────────────────── */

/* Heap-owned context handed to the worker thread. The strings are
 * copies (the source widgets / state may have changed by the time the
 * worker reads them), the section pointer is stable for the lifetime
 * of the GUI (s_sections is a static array). The transport pointer is
 * borrowed from AppState - its lifetime exceeds any flash. */
typedef struct {
    ScFlashSection *section;
    AppState *state;
    const ScTransport *transport;
    size_t module_index;
    char device_path[512];
    char uid_hex[64];
    char uf2_path[512];
    char manifest_path[512]; /* may be empty */
} FlashCtx;

/* Marshalled event from worker -> main loop via g_idle_add. */
typedef enum {
    FLASH_EV_PROGRESS,
    FLASH_EV_COMPLETE,
} FlashEventKind;

typedef struct {
    FlashEventKind kind;
    ScFlashSection *section;
    AppState *state;
    /* Progress fields (kind == FLASH_EV_PROGRESS). */
    ScFlashPhase phase;
    uint64_t bytes_written;
    uint64_t bytes_total;
    /* Completion fields (kind == FLASH_EV_COMPLETE). */
    ScFlashStatus status;
    char message[512];
} FlashEvent;

static gboolean flash_idle_apply(gpointer user_data)
{
    FlashEvent *ev = (FlashEvent *)user_data;
    if (ev == NULL || ev->section == NULL || ev->state == NULL) {
        g_free(ev);
        return G_SOURCE_REMOVE;
    }
    ScFlashSection *s = ev->section;

    if (ev->kind == FLASH_EV_PROGRESS) {
        char buf[192];
        double frac = 0.0;
        double creep_from = 0.0;
        section_show_progress_bar(s);
        double current = sc_progressbar_get_fraction(s->progress_bar);
        if (current < 0.0) current = 0.0;
        if (current > 1.0) current = 1.0;
        if (ev->phase == SC_FLASH_PHASE_COPY && ev->bytes_total > 0u) {
            /* Inside COPY - interpolate within the phase's fraction
             * range. Per-chunk events arrive every 64 KiB so the bar
             * sweeps smoothly across this slice. */
            const double phase_start =
                phase_start_fraction(SC_FLASH_PHASE_COPY);
            const double phase_end =
                phase_end_fraction(SC_FLASH_PHASE_COPY);
            const double in_phase = (double)ev->bytes_written /
                                    (double)ev->bytes_total;
            const double target =
                phase_start + (phase_end - phase_start) * in_phase;
            /* Smooth large jumps (notably COPY entry after quick early
             * phases) so the operator sees a continuous ramp. */
            if (target > current + SC_UI_FLASH_COPY_MAX_STEP) {
                frac = current + SC_UI_FLASH_COPY_MAX_STEP;
            } else {
                frac = target;
            }
            creep_from = frac;
            const unsigned pct = (unsigned)((ev->bytes_written * 100u) /
                                            ev->bytes_total);
            (void)snprintf(buf, sizeof(buf),
                           sc_i18n_string_get(SC_I18N_FLASH_STATUS_COPY_FRACTION_FMT),
                           pct);
        } else {
            /* Phase boundary - jump to the next phase's start fraction.
             * The bar shows a determinate position even during long
             * waits (WAIT_BOOTSEL, WAIT_REENUM); the status label
             * names which phase the host is sitting in. */
            const double target = phase_start_fraction(ev->phase);
            /* Never jump abruptly at phase boundaries. Advance only a
             * small step immediately; the creep ticker fills the rest. */
            if (target > current + SC_UI_FLASH_PHASE_MAX_STEP) {
                frac = current + SC_UI_FLASH_PHASE_MAX_STEP;
            } else if (target < current) {
                frac = current;
            } else {
                frac = target;
            }
            creep_from = frac;
            (void)snprintf(buf, sizeof(buf),
                           sc_i18n_string_get(SC_I18N_FLASH_STATUS_RUNNING_FMT),
                           sc_flash_phase_name(ev->phase));
        }
        section_set_progress_visual(s, frac);
        if (ev->phase == SC_FLASH_PHASE_COPY && ev->bytes_total > 0u) {
            /* COPY drives the bar from per-chunk events - no creep
             * needed and the timer would fight the real fraction. */
            stop_phase_creep(s);
        } else {
            /* Re-arm the creep so the bar slides smoothly toward the
             * next phase's start fraction during long waits
             * (WAIT_BOOTSEL, WAIT_REENUM, AUTH). Each phase event
             * resets the creep's anchor to the new phase_start. */
            start_phase_creep(s, ev->phase, creep_from);
        }
        section_set_status(s, buf);
    } else { /* FLASH_EV_COMPLETE */
        stop_phase_creep(s);
        char buf[640];
        if (ev->status == SC_FLASH_STATUS_OK) {
            (void)snprintf(buf, sizeof(buf),
                           sc_i18n_string_get(SC_I18N_FLASH_STATUS_DONE_FMT),
                           ev->message);
        } else {
            (void)snprintf(buf, sizeof(buf),
                           sc_i18n_string_get(SC_I18N_FLASH_STATUS_FAILED_FMT),
                           sc_flash_status_name(ev->status), ev->message);
        }
        section_set_status(s, buf);

        if (s->progress_bar != NULL) {
            sc_progressbar_set_fraction(s->progress_bar, 0.0);
        }
        if (s->module_index < SC_MODULE_COUNT) {
            s_last_flash_result[s->module_index] =
                (ev->status == SC_FLASH_STATUS_OK)
                    ? SC_LAST_FLASH_OK
                    : SC_LAST_FLASH_FAIL;
        }
        section_show_progress_result(s, ev->status == SC_FLASH_STATUS_OK);

        sc_flash_tab_set_lock(ev->state, false);

        /* On success, kick off a full re-detection so all sections see
         * the new fw_version / build_id. The detection worker raises
         * its own lock independently. */
        if (ev->status == SC_FLASH_STATUS_OK) {
            sc_detection_start_async(ev->state);
        }
    }
    g_free(ev);
    return G_SOURCE_REMOVE;
}

/* Always marshal worker events through the GTK main context. This
 * keeps all widget mutations on the UI thread and avoids depending on
 * whichever thread-default context happens to be current. */
static void flash_post_to_ui(FlashEvent *ev)
{
    if (ev == NULL) {
        return;
    }
    g_main_context_invoke_full(NULL, G_PRIORITY_DEFAULT_IDLE,
                               flash_idle_apply, ev, NULL);
}

/* Progress callback called from the worker thread. Allocates a small
 * event and posts it to the main loop; never touches widgets directly. */
static void flash_progress_marshal(ScFlashPhase phase, uint64_t bytes_written,
                                    uint64_t bytes_total, void *user)
{
    FlashCtx *ctx = (FlashCtx *)user;
    if (ctx == NULL) return;
    FlashEvent *ev = g_new0(FlashEvent, 1);
    ev->kind = FLASH_EV_PROGRESS;
    ev->section = ctx->section;
    ev->state = ctx->state;
    ev->phase = phase;
    ev->bytes_written = bytes_written;
    ev->bytes_total = bytes_total;
    flash_post_to_ui(ev);
}

static gpointer flash_worker_main(gpointer user_data)
{
    FlashCtx *ctx = (FlashCtx *)user_data;

    char err[512] = {0};
    const char *manifest = (ctx->manifest_path[0] != '\0')
                               ? ctx->manifest_path : NULL;
    const ScFlashStatus rc = sc_core_flash(
        ctx->transport, ctx->module_index, ctx->device_path, ctx->uid_hex,
        ctx->uf2_path, manifest, NULL,
        flash_progress_marshal, ctx,
        err, sizeof(err));

    FlashEvent *ev = g_new0(FlashEvent, 1);
    ev->kind = FLASH_EV_COMPLETE;
    ev->section = ctx->section;
    ev->state = ctx->state;
    ev->status = rc;
    (void)snprintf(ev->message, sizeof(ev->message), "%s", err);
    flash_post_to_ui(ev);

    g_free(ctx);
    return NULL;
}

static void on_flash_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ScFlashSection *s = (ScFlashSection *)user_data;
    if (s == NULL || s->state == NULL) return;

    /* Pre-flight gates with operator-friendly diagnostics. */
    const ScModuleStatus *status = sc_core_module_status(&s->state->core,
                                                         s->module_index);
    if (status == NULL || !status->detected ||
        status->port_path[0] == '\0' ||
        !status->hello_identity.valid ||
        status->hello_identity.uid[0] == '\0') {
        section_set_status(s,
            sc_i18n_string_get(SC_I18N_FLASH_STATUS_NEED_DETECTION));
        return;
    }
    const char *uf2 = sc_flash_paths_get_uf2(&s->state->flash_paths,
                                              s->module_name);
    if (uf2 == NULL || uf2[0] == '\0') {
        section_set_status(s,
            sc_i18n_string_get(SC_I18N_FLASH_STATUS_NEED_UF2));
        return;
    }

    FlashCtx *ctx = g_new0(FlashCtx, 1);
    ctx->section = s;
    ctx->state = s->state;
    ctx->transport = &s->state->core.transport;
    ctx->module_index = s->module_index;
    (void)snprintf(ctx->device_path, sizeof(ctx->device_path), "%s",
                   status->port_path);
    (void)snprintf(ctx->uid_hex, sizeof(ctx->uid_hex), "%s",
                   status->hello_identity.uid);
    (void)snprintf(ctx->uf2_path, sizeof(ctx->uf2_path), "%s", uf2);
    const char *manifest = sc_flash_paths_get_manifest(&s->state->flash_paths,
                                                        s->module_name);
    if (manifest != NULL && manifest[0] != '\0') {
        (void)snprintf(ctx->manifest_path, sizeof(ctx->manifest_path),
                       "%s", manifest);
    }

    sc_flash_tab_set_lock(s->state, true);
    if (s->module_index < SC_MODULE_COUNT) {
        s_last_flash_result[s->module_index] = SC_LAST_FLASH_NONE;
    }
    if (s->progress_bar != NULL) {
        section_show_progress_bar(s);
        section_set_progress_visual(s, 0.0);
    }
    section_set_status(s, sc_flash_phase_name(SC_FLASH_PHASE_FORMAT_CHECK));

    GThread *worker = g_thread_new("sc_flash_worker",
                                    flash_worker_main, ctx);
    g_thread_unref(worker); /* detached - worker frees ctx itself. */
}

/* ── section construction ──────────────────────────────────────────── */

static void section_init_clean(ScFlashSection *s)
{
    /* Cancel a pending creep tick so it does not fire against a
     * widget pointer the rebuild is about to zero out. Safe no-op
     * when no timer is armed. */
    stop_phase_creep(s);
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
    s->progress_area = NULL;
    s->progress_slot = NULL;
    s->progress_bar = NULL;
    s->progress_result_label = NULL;
    s->creep_timer_id = 0u;
    s->creep_phase = SC_FLASH_PHASE_FORMAT_CHECK;
    s->creep_from_fraction = 0.0;
    s->creep_anchor_ms = 0;
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
    gtk_widget_set_valign(action_row, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(body), action_row);

    s->flash_btn = gtk_button_new_with_label(
        sc_i18n_string_get(SC_I18N_FLASH_BTN_FLASH));
    g_signal_connect(s->flash_btn, "clicked",
                     G_CALLBACK(on_flash_clicked), s);
    gtk_box_append(GTK_BOX(action_row), s->flash_btn);

    /* Dedicated fixed-height area; bar and result label share exactly
     * the same vertical space. */
    s->progress_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(s->progress_area, TRUE);
    gtk_widget_set_vexpand(s->progress_area, FALSE);
    gtk_widget_set_valign(s->progress_area, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(s->progress_area, -1,
                                SC_UI_FLASH_PROGRESS_HEIGHT_PX);
    gtk_widget_set_visible(s->progress_area, FALSE);
    gtk_box_append(GTK_BOX(action_row), s->progress_area);

    s->progress_slot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(s->progress_slot, TRUE);
    gtk_widget_set_vexpand(s->progress_slot, FALSE);
    gtk_widget_set_valign(s->progress_slot, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(s->progress_slot, -1,
                                SC_UI_FLASH_PROGRESS_HEIGHT_PX);
    gtk_widget_set_visible(s->progress_slot, FALSE);
    gtk_box_append(GTK_BOX(s->progress_area), s->progress_slot);

    s->progress_bar = sc_progressbar_new(SC_UI_FLASH_PROGRESS_HEIGHT_PX);
    gtk_widget_set_halign(s->progress_bar, GTK_ALIGN_FILL);
    gtk_widget_set_valign(s->progress_bar, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(s->progress_bar, TRUE);
    gtk_box_append(GTK_BOX(s->progress_slot), s->progress_bar);

    s->progress_result_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(s->progress_result_label), 0.0f);
    gtk_label_set_yalign(GTK_LABEL(s->progress_result_label), 0.5f);
    gtk_widget_set_halign(s->progress_result_label, GTK_ALIGN_FILL);
    gtk_widget_set_valign(s->progress_result_label, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(s->progress_result_label, -1,
                                SC_UI_FLASH_PROGRESS_HEIGHT_PX);
    gtk_widget_set_hexpand(s->progress_result_label, TRUE);
    gtk_widget_set_margin_start(s->progress_result_label, 4);
    gtk_box_append(GTK_BOX(s->progress_slot), s->progress_result_label);
    gtk_widget_set_visible(s->progress_result_label, FALSE);

    s->status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(s->status_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(s->status_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(s->status_label), TRUE);
    gtk_box_append(GTK_BOX(body), s->status_label);

    if (module_index < SC_MODULE_COUNT) {
        if (s_last_flash_result[module_index] == SC_LAST_FLASH_OK) {
            section_show_progress_result(s, true);
        } else if (s_last_flash_result[module_index] == SC_LAST_FLASH_FAIL) {
            section_show_progress_result(s, false);
        }
    }

    section_recompute_status(s);
    return s->frame;
}

/* ── public API ────────────────────────────────────────────────────── */

void sc_flash_tab_refresh_sensitivity(AppState *state)
{
    if (state == 0 || state->flash_tab_root == 0) {
        return;
    }
    /* Keep the tab interactive while a flash is running so GTK does
     * not render the progress widget in an insensitive (all-gray)
     * state even if transient detection status flips occur. */
    if (state->flash_in_progress) {
        gtk_widget_set_sensitive(state->flash_tab_root, TRUE);
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

    /* Detect button - sc_detection.c owns the runtime state but the
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

    /* Persisted paths first - every freshly-rebuilt section reads
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

    /* Initial body - placeholder until first detection. */
    sc_flash_tab_rebuild_sections(state);
    sc_flash_tab_refresh_sensitivity(state);
    return flash_tab_root;
}
