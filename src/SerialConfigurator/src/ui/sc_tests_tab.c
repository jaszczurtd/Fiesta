#include "sc_tests_tab.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sc_core.h"
#include "sc_i18n.h"
#include "sc_protocol.h"
#include "sc_tests.h"
#include "sc_text.h"

/* ── Wire tokens shown to the operator ─────────────────────────────── */

typedef struct {
  const char *token;
  ScI18nKey key;
} TokenLabel;

typedef struct {
  const char *name;
  ScI18nKey title;
  ScI18nKey description;
} TestLabel;

static const TestLabel k_test_labels[] = {
    {SC_TEST_NAME_CYCLIC, SC_I18N_TEST_NAME_CYCLIC, SC_I18N_TEST_DESC_CYCLIC},
    {SC_TEST_NAME_RANDOM, SC_I18N_TEST_NAME_RANDOM, SC_I18N_TEST_DESC_RANDOM},
    {SC_TEST_NAME_TOP, SC_I18N_TEST_NAME_TOP, SC_I18N_TEST_DESC_TOP},
    {SC_TEST_NAME_TOPZERO, SC_I18N_TEST_NAME_TOPZERO,
     SC_I18N_TEST_DESC_TOPZERO},
    {SC_TEST_NAME_POT, SC_I18N_TEST_NAME_POT, SC_I18N_TEST_DESC_POT},
};

static const TokenLabel k_param_labels[] = {
    {SC_TEST_PARAM_CYCLIC_PASSES, SC_I18N_TEST_PARAM_PASSES},
    {SC_TEST_PARAM_CYCLIC_CYCLES, SC_I18N_TEST_PARAM_CYCLES},
    {SC_TEST_PARAM_RANDOM_DURATION, SC_I18N_TEST_PARAM_DURATION},
    {SC_TEST_PARAM_RANDOM_HOLD, SC_I18N_TEST_PARAM_HOLD},
    {SC_TEST_PARAM_TOP_DWELL, SC_I18N_TEST_PARAM_DWELL},
    {SC_TEST_PARAM_TOP_SERIES, SC_I18N_TEST_PARAM_SERIES},
    {SC_TEST_PARAM_TOPZERO_DWELL, SC_I18N_TEST_PARAM_DWELL},
    {SC_TEST_PARAM_TOPZERO_SERIES, SC_I18N_TEST_PARAM_SERIES},
    {SC_TEST_PARAM_POT_RATE, SC_I18N_TEST_PARAM_RATE},
    {SC_TEST_PARAM_POT_HOLD, SC_I18N_TEST_PARAM_HOLD_TOP},
    {SC_TEST_PARAM_POT_REST, SC_I18N_TEST_PARAM_REST},
    {SC_TEST_PARAM_POT_PASSES, SC_I18N_TEST_PARAM_PASSES},
};

static const TokenLabel k_result_labels[] = {
    {SC_TEST_RESULT_DONE, SC_I18N_TEST_RESULT_DONE},
    {SC_TEST_RESULT_OK, SC_I18N_TEST_RESULT_OK},
    {SC_TEST_RESULT_FAILED, SC_I18N_TEST_RESULT_FAILED},
    {SC_TEST_RESULT_STOPPED, SC_I18N_TEST_RESULT_STOPPED},
    {SC_TEST_RESULT_HOST_LOST, SC_I18N_TEST_RESULT_HOST_LOST},
    {SC_TEST_RESULT_SESSION_END, SC_I18N_TEST_RESULT_SESSION_END},
    {SC_TEST_RESULT_ENGINE_RUNNING, SC_I18N_TEST_RESULT_ENGINE_RUNNING},
};

static const TokenLabel k_phase_labels[] = {
    {SC_TEST_PHASE_RISE, SC_I18N_TEST_PHASE_RISE},
    {SC_TEST_PHASE_HOLD, SC_I18N_TEST_PHASE_HOLD},
    {SC_TEST_PHASE_FALL, SC_I18N_TEST_PHASE_FALL},
    {SC_TEST_PHASE_REST, SC_I18N_TEST_PHASE_REST},
};

/* Translated text of @p token, or the token itself when it is unknown. */
static const char *token_text(const TokenLabel *labels, size_t count,
                              const char *token) {
  for (size_t i = 0u; i < count; ++i) {
    if (strcmp(labels[i].token, token) == 0) {
      return sc_i18n_string_get(labels[i].key);
    }
  }
  return token;
}

static const TestLabel *test_label(const char *name) {
  for (size_t i = 0u; i < G_N_ELEMENTS(k_test_labels); ++i) {
    if (strcmp(k_test_labels[i].name, name) == 0) {
      return &k_test_labels[i];
    }
  }
  return NULL;
}

static const char *test_title(const char *name) {
  const TestLabel *label = test_label(name);
  return (label != NULL) ? sc_i18n_string_get(label->title) : name;
}

/* Units the operator reads; a count needs none. */
static const char *unit_text(const char *unit) {
  if (strcmp(unit, SC_TEST_UNIT_PCT_PER_S) == 0) {
    return "%/s";
  }
  if (strcmp(unit, SC_TEST_UNIT_COUNT) == 0) {
    return "";
  }
  return unit;
}

/* ── State ─────────────────────────────────────────────────────────── */

typedef struct TestsTab TestsTab;

typedef struct {
  char id[SC_TEST_ID_MAX];
  int32_t applied; /* value in force on the ECU */
  int32_t default_value;
  GtkAdjustment *adjustment;
} ParamRow;

typedef struct {
  TestsTab *tab;
  char name[SC_TEST_ID_MAX];
  size_t first_param;
  size_t param_count;
  GtkWidget *run_button;
} TestCard;

struct TestsTab {
  AppState *state;
  size_t module_index;
  ScTestCatalog catalog;
  ParamRow params[SC_TESTS_MAX * SC_TESTS_PARAMS_MAX];
  size_t param_count;
  TestCard cards[SC_TESTS_MAX];
  size_t card_count;
  GtkWidget *root;
  GtkWidget *sequence_button;
  GtkWidget *skip_button;
  GtkWidget *stop_button;
  GtkWidget *message;
  GtkWidget *status_title;
  GtkWidget *status_sequence;
  GtkWidget *status_elapsed;
  GtkWidget *demand_label;
  GtkWidget *demand_bar;
  GtkWidget *position_label;
  GtkWidget *position_bar;
  GtkWidget *status_progress;
  GtkWidget *status_last;
  guint poll_id;
  bool running;
  /* A test started here that has not ended yet: polled even while the
   * page is hidden, because the polling keeps it alive on the ECU. */
  bool owns_test;
  uint32_t runs_before_start;
};

static void tests_tab_free(gpointer data) {
  TestsTab *t = (TestsTab *)data;
  if (t == NULL) {
    return;
  }
  if (t->poll_id != 0u) {
    g_source_remove(t->poll_id);
  }
  g_free(t);
}

static void set_message(TestsTab *t, const char *text) {
  gtk_label_set_text(GTK_LABEL(t->message), (text != NULL) ? text : "");
}

/* ── Status panel ──────────────────────────────────────────────────── */

static void progress_append(char *buffer, size_t size, const char *fragment) {
  const size_t used = strlen(buffer);
  if (used + 1u >= size) {
    return;
  }
  (void)snprintf(&buffer[used], size - used, "%s%s",
                 (used != 0u) ? "  ·  " : "", fragment);
}

/* A value with its total, e.g. cycle 2/6; the total is shown with the
 * value, so it is skipped when its own key comes up. */
static const struct {
  const char *key;
  const char *total_key;
  ScI18nKey format;
} k_counted_fields[] = {
    {SC_TEST_FIELD_CYCLE, SC_TEST_FIELD_CYCLES,
     SC_I18N_TEST_PROGRESS_CYCLE_FMT},
    {SC_TEST_FIELD_PASS, SC_TEST_FIELD_PASSES, SC_I18N_TEST_PROGRESS_PASS_FMT},
    {SC_TEST_FIELD_SERIES, SC_TEST_FIELD_SERIES_COUNT,
     SC_I18N_TEST_PROGRESS_SERIES_FMT},
};

static bool is_total_key(const char *key) {
  for (size_t i = 0u; i < G_N_ELEMENTS(k_counted_fields); ++i) {
    if (strcmp(key, k_counted_fields[i].total_key) == 0) {
      return true;
    }
  }
  return false;
}

/* One progress value in the operator's words. */
static void format_field(const ScTestStatus *status, const ScTestField *field,
                         char *out, size_t out_size) {
  const char *key = field->key;
  const int value = (int)field->value;
  out[0] = '\0';
  if (field->is_text) {
    if (strcmp(key, SC_TEST_FIELD_PHASE) == 0) {
      (void)snprintf(out, out_size,
                     sc_i18n_string_get(SC_I18N_TEST_PROGRESS_PHASE_FMT),
                     token_text(k_phase_labels, G_N_ELEMENTS(k_phase_labels),
                                field->text));
    } else {
      (void)snprintf(out, out_size, "%s=%s", key, field->text);
    }
    return;
  }
  for (size_t i = 0u; i < G_N_ELEMENTS(k_counted_fields); ++i) {
    if (strcmp(key, k_counted_fields[i].key) == 0) {
      int32_t total = 0;
      (void)sc_tests_status_field(status, k_counted_fields[i].total_key,
                                  &total);
      (void)snprintf(out, out_size,
                     sc_i18n_string_get(k_counted_fields[i].format), value,
                     (int)total);
      return;
    }
  }
  if (strcmp(key, SC_TEST_FIELD_PROFILE) == 0) {
    (void)snprintf(out, out_size,
                   sc_i18n_string_get(SC_I18N_TEST_PROGRESS_PROFILE_FMT),
                   value);
  } else if (strcmp(key, SC_TEST_FIELD_RATE) == 0) {
    (void)snprintf(out, out_size,
                   sc_i18n_string_get(SC_I18N_TEST_PROGRESS_RATE_FMT), value);
  } else if (strcmp(key, SC_TEST_FIELD_TARGET) == 0) {
    (void)snprintf(out, out_size,
                   sc_i18n_string_get(SC_I18N_TEST_PROGRESS_TARGET_FMT), value);
  } else if (strcmp(key, SC_TEST_FIELD_HOLD_LEFT_MS) == 0) {
    (void)snprintf(out, out_size,
                   sc_i18n_string_get(SC_I18N_TEST_PROGRESS_NEXT_FMT),
                   (double)value / 1000.0);
  } else if (strcmp(key, SC_TEST_FIELD_LEFT_S) == 0) {
    (void)snprintf(out, out_size,
                   sc_i18n_string_get(SC_I18N_TEST_PROGRESS_LEFT_FMT), value);
  } else if (strcmp(key, SC_TEST_FIELD_SETPOINT_X10) == 0) {
    (void)snprintf(out, out_size,
                   sc_i18n_string_get(SC_I18N_TEST_PROGRESS_SETPOINT_FMT),
                   (double)value / 10.0);
  } else if (!is_total_key(key)) {
    (void)snprintf(out, out_size, "%s=%d", key, value);
  } else {
    /* Shown with its counted value. */
  }
}

static void set_drive(GtkWidget *label, GtkWidget *bar, ScI18nKey format,
                      bool valid, int32_t value_x10) {
  if (!valid) {
    gtk_label_set_text(GTK_LABEL(label), "");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), 0.0);
    return;
  }
  const double percent = (double)value_x10 / 10.0;
  char text[64];
  (void)snprintf(text, sizeof(text), sc_i18n_string_get(format), percent);
  gtk_label_set_text(GTK_LABEL(label), text);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar),
                                CLAMP(percent / 100.0, 0.0, 1.0));
}

static void update_sensitivity(TestsTab *t) {
  gtk_widget_set_sensitive(t->sequence_button, !t->running);
  gtk_widget_set_sensitive(t->skip_button, t->running);
  gtk_widget_set_sensitive(t->stop_button, t->running);
  for (size_t i = 0u; i < t->card_count; ++i) {
    gtk_widget_set_sensitive(t->cards[i].run_button, !t->running);
  }
}

static void render_status(TestsTab *t, const ScTestStatus *status) {
  char text[256];
  t->running = status->running;
  if (status->running) {
    const bool here = strcmp(status->source, SC_TEST_SOURCE_SC) == 0;
    (void)snprintf(text, sizeof(text),
                   sc_i18n_string_get(SC_I18N_TESTS_RUNNING_FMT),
                   test_title(status->test),
                   sc_i18n_string_get(here ? SC_I18N_TESTS_SOURCE_SC
                                           : SC_I18N_TESTS_SOURCE_CONSOLE));
    char markup[320];
    char *escaped = g_markup_escape_text(text, -1);
    (void)snprintf(markup, sizeof(markup), "<b>%s</b>", escaped);
    g_free(escaped);
    gtk_label_set_markup(GTK_LABEL(t->status_title), markup);
  } else {
    gtk_label_set_text(GTK_LABEL(t->status_title),
                       sc_i18n_string_get(SC_I18N_TESTS_IDLE));
  }

  text[0] = '\0';
  if (status->running && status->seq_count != 0u) {
    (void)snprintf(text, sizeof(text),
                   sc_i18n_string_get(SC_I18N_TESTS_SEQUENCE_STEP_FMT),
                   status->seq_index, status->seq_count);
  }
  gtk_label_set_text(GTK_LABEL(t->status_sequence), text);

  text[0] = '\0';
  if (status->running) {
    (void)snprintf(text, sizeof(text),
                   sc_i18n_string_get(SC_I18N_TESTS_ELAPSED_FMT),
                   (double)status->elapsed_ms / 1000.0);
  }
  gtk_label_set_text(GTK_LABEL(t->status_elapsed), text);

  set_drive(t->demand_label, t->demand_bar, SC_I18N_TESTS_DEMAND_FMT,
            status->drive_valid, status->demand_x10);
  set_drive(t->position_label, t->position_bar, SC_I18N_TESTS_POSITION_FMT,
            status->drive_valid, status->position_x10);
  if (!status->drive_valid) {
    gtk_label_set_text(GTK_LABEL(t->demand_label),
                       sc_i18n_string_get(SC_I18N_TESTS_DRIVE_UNKNOWN));
  }

  char progress[512];
  progress[0] = '\0';
  for (size_t i = 0u; status->running && i < status->field_count; ++i) {
    char fragment[96];
    format_field(status, &status->fields[i], fragment, sizeof(fragment));
    if (fragment[0] != '\0') {
      progress_append(progress, sizeof(progress), fragment);
    }
  }
  gtk_label_set_text(GTK_LABEL(t->status_progress), progress);

  text[0] = '\0';
  if (status->has_last) {
    (void)snprintf(text, sizeof(text),
                   sc_i18n_string_get(SC_I18N_TESTS_LAST_FMT),
                   test_title(status->last),
                   token_text(k_result_labels, G_N_ELEMENTS(k_result_labels),
                              status->result));
  }
  gtk_label_set_text(GTK_LABEL(t->status_last), text);

  /* A test started here has ended once a later start has finished, or
   * once someone else's test replaced it. */
  if (t->owns_test && status->runs != t->runs_before_start &&
      (!status->running || strcmp(status->source, SC_TEST_SOURCE_SC) != 0)) {
    t->owns_test = false;
  }
  update_sensitivity(t);
}

/* Read and show the status; false when it could not be read. */
static bool refresh_status(TestsTab *t, ScTestStatus *out) {
  char err[512];
  err[0] = '\0';
  if (!sc_tests_get_status(&t->state->core, t->module_index, out, err,
                           sizeof(err))) {
    char text[640];
    (void)snprintf(text, sizeof(text),
                   sc_i18n_string_get(SC_I18N_TESTS_STATUS_FAILED_FMT), err);
    set_message(t, text);
    return false;
  }
  render_status(t, out);
  return true;
}

static gboolean on_poll(gpointer user_data) {
  TestsTab *t = (TestsTab *)user_data;
  const AppState *s = t->state;
  if (!s->connected || s->detection_in_progress || s->flash_in_progress) {
    return G_SOURCE_CONTINUE;
  }
  if (!t->owns_test && !gtk_widget_get_mapped(t->root)) {
    return G_SOURCE_CONTINUE;
  }
  ScTestStatus status;
  (void)refresh_status(t, &status);
  return G_SOURCE_CONTINUE;
}

/* ── Actions ───────────────────────────────────────────────────────── */

static bool authenticate(TestsTab *t, char *port, size_t port_size) {
  const ScModuleStatus *st =
      sc_core_module_status(&t->state->core, t->module_index);
  char err[512];
  err[0] = '\0';
  if (st == NULL || !st->detected || st->port_path[0] == '\0') {
    sc_text_copy(err, sizeof(err), "module not detected");
  } else if (sc_core_authenticate(&t->state->core.transport, st->port_path, err,
                                  sizeof(err)) == SC_AUTH_OK) {
    sc_text_copy(port, port_size, st->port_path);
    return true;
  }
  char text[640];
  (void)snprintf(text, sizeof(text),
                 sc_i18n_string_get(SC_I18N_VALUES_AUTH_FAILED_FMT), err);
  set_message(t, text);
  return false;
}

/* Reason for a refused request in the operator's words. */
static void refusal_text(ScTestActionStatus status, const char *err, char *out,
                         size_t out_size) {
  const char *reason = err;
  if (status == SC_TEST_ACTION_ERR_ENGINE_RUNNING) {
    reason = sc_i18n_string_get(SC_I18N_TESTS_ENGINE_RUNNING);
  } else if (status == SC_TEST_ACTION_ERR_BUSY) {
    reason = sc_i18n_string_get(SC_I18N_TESTS_BUSY);
  }
  (void)snprintf(out, out_size, sc_i18n_string_get(SC_I18N_TESTS_REFUSED_FMT),
                 reason);
}

/* Send the edited parameters of rows [first, first + count). */
static bool push_params(TestsTab *t, const char *port, size_t first,
                        size_t count) {
  for (size_t i = first; i < first + count; ++i) {
    ParamRow *row = &t->params[i];
    const int32_t value = (int32_t)gtk_adjustment_get_value(row->adjustment);
    if (value == row->applied) {
      continue;
    }
    char err[512];
    err[0] = '\0';
    const ScTestActionStatus rc = sc_tests_set_param(
        &t->state->core.transport, port, row->id, value, err, sizeof(err));
    if (rc != SC_TEST_ACTION_OK) {
      char text[700];
      (void)snprintf(
          text, sizeof(text),
          sc_i18n_string_get(SC_I18N_TESTS_PARAM_FAILED_FMT),
          token_text(k_param_labels, G_N_ELEMENTS(k_param_labels), row->id),
          err);
      set_message(t, text);
      return false;
    }
    row->applied = value;
  }
  return true;
}

static void start_test(TestsTab *t, const char *name, const char *title,
                       size_t first_param, size_t param_count) {
  ScTestStatus before;
  char port[SC_PORT_PATH_MAX];
  if (!refresh_status(t, &before) || !authenticate(t, port, sizeof(port)) ||
      !push_params(t, port, first_param, param_count)) {
    return;
  }
  char err[512];
  err[0] = '\0';
  const ScTestActionStatus rc =
      sc_tests_run(&t->state->core.transport, port, name, err, sizeof(err));
  char text[640];
  if (rc != SC_TEST_ACTION_OK) {
    refusal_text(rc, err, text, sizeof(text));
    set_message(t, text);
    return;
  }
  t->owns_test = true;
  t->runs_before_start = before.runs;
  (void)snprintf(text, sizeof(text),
                 sc_i18n_string_get(SC_I18N_TESTS_STARTED_FMT), title);
  set_message(t, text);
  ScTestStatus after;
  (void)refresh_status(t, &after);
}

static void on_run_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  TestCard *card = (TestCard *)user_data;
  start_test(card->tab, card->name, test_title(card->name), card->first_param,
             card->param_count);
}

static void on_sequence_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  TestsTab *t = (TestsTab *)user_data;
  start_test(t, SC_TEST_SEQUENCE,
             sc_i18n_string_get(SC_I18N_TESTS_BTN_RUN_SEQUENCE), 0u,
             t->param_count);
}

static void run_control(TestsTab *t,
                        ScTestActionStatus (*action)(const ScTransport *,
                                                     const char *, char *,
                                                     size_t),
                        ScI18nKey done) {
  char port[SC_PORT_PATH_MAX];
  if (!authenticate(t, port, sizeof(port))) {
    return;
  }
  char err[512];
  err[0] = '\0';
  const ScTestActionStatus rc =
      action(&t->state->core.transport, port, err, sizeof(err));
  if (rc != SC_TEST_ACTION_OK) {
    char text[640];
    refusal_text(rc, err, text, sizeof(text));
    set_message(t, text);
    return;
  }
  set_message(t, sc_i18n_string_get(done));
  ScTestStatus status;
  (void)refresh_status(t, &status);
}

static void on_skip_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  run_control((TestsTab *)user_data, sc_tests_skip,
              SC_I18N_TESTS_SKIP_REQUESTED);
}

static void on_stop_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  run_control((TestsTab *)user_data, sc_tests_stop,
              SC_I18N_TESTS_STOP_REQUESTED);
}

static void on_defaults_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  const TestCard *card = (const TestCard *)user_data;
  for (size_t i = card->first_param; i < card->first_param + card->param_count;
       ++i) {
    const ParamRow *row = &card->tab->params[i];
    gtk_adjustment_set_value(row->adjustment, (double)row->default_value);
  }
}

/* ── Layout ────────────────────────────────────────────────────────── */

static GtkWidget *dim_label(const char *text) {
  GtkWidget *label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_widget_add_css_class(label, "dim-label");
  return label;
}

static GtkWidget *left_label(const char *text) {
  GtkWidget *label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  return label;
}

static GtkWidget *centered_message(const char *text) {
  GtkWidget *label = gtk_label_new(text);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_widget_set_vexpand(label, TRUE);
  return label;
}

static GtkWidget *build_status_panel(TestsTab *t) {
  GtkWidget *frame = gtk_frame_new(NULL);
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_widget_set_margin_start(grid, 8);
  gtk_widget_set_margin_end(grid, 8);
  gtk_widget_set_margin_top(grid, 8);
  gtk_widget_set_margin_bottom(grid, 8);
  gtk_frame_set_child(GTK_FRAME(frame), grid);

  t->status_title = left_label(sc_i18n_string_get(SC_I18N_TESTS_IDLE));
  gtk_grid_attach(GTK_GRID(grid), t->status_title, 0, 0, 2, 1);
  t->status_sequence = left_label("");
  gtk_grid_attach(GTK_GRID(grid), t->status_sequence, 0, 1, 1, 1);
  t->status_elapsed = left_label("");
  gtk_grid_attach(GTK_GRID(grid), t->status_elapsed, 1, 1, 1, 1);

  t->demand_label = left_label("");
  gtk_widget_set_size_request(t->demand_label, 220, -1);
  gtk_grid_attach(GTK_GRID(grid), t->demand_label, 0, 2, 1, 1);
  t->demand_bar = gtk_progress_bar_new();
  gtk_widget_set_hexpand(t->demand_bar, TRUE);
  gtk_widget_set_valign(t->demand_bar, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), t->demand_bar, 1, 2, 1, 1);

  t->position_label = left_label("");
  gtk_grid_attach(GTK_GRID(grid), t->position_label, 0, 3, 1, 1);
  t->position_bar = gtk_progress_bar_new();
  gtk_widget_set_hexpand(t->position_bar, TRUE);
  gtk_widget_set_valign(t->position_bar, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), t->position_bar, 1, 3, 1, 1);

  t->status_progress = left_label("");
  gtk_label_set_wrap(GTK_LABEL(t->status_progress), TRUE);
  gtk_grid_attach(GTK_GRID(grid), t->status_progress, 0, 4, 2, 1);
  t->status_last = left_label("");
  gtk_grid_attach(GTK_GRID(grid), t->status_last, 0, 5, 2, 1);
  return frame;
}

static GtkWidget *build_card(TestsTab *t, const ScTestEntry *entry) {
  TestCard *card = &t->cards[t->card_count++];
  card->tab = t;
  sc_text_copy(card->name, sizeof(card->name), entry->name);
  card->first_param = t->param_count;
  card->param_count = entry->param_count;

  const TestLabel *label = test_label(entry->name);
  GtkWidget *frame = gtk_frame_new(test_title(entry->name));
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_start(box, 8);
  gtk_widget_set_margin_end(box, 8);
  gtk_widget_set_margin_top(box, 6);
  gtk_widget_set_margin_bottom(box, 8);
  gtk_frame_set_child(GTK_FRAME(frame), box);

  if (label != NULL) {
    GtkWidget *description = left_label(sc_i18n_string_get(label->description));
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_box_append(GTK_BOX(box), description);
  }
  gtk_box_append(GTK_BOX(box),
                 dim_label(sc_i18n_string_get(entry->in_sequence
                                                  ? SC_I18N_TESTS_IN_SEQUENCE
                                                  : SC_I18N_TESTS_ON_REQUEST)));

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
  gtk_box_append(GTK_BOX(box), grid);
  for (size_t p = 0u; p < entry->param_count; ++p) {
    const ScTestParam *param = &entry->params[p];
    ParamRow *row = &t->params[t->param_count++];
    sc_text_copy(row->id, sizeof(row->id), param->id);
    row->applied = param->value;
    row->default_value = param->default_value;
    row->adjustment =
        gtk_adjustment_new((double)param->value, (double)param->min,
                           (double)param->max, 1.0, 10.0, 0.0);

    GtkWidget *name = left_label(
        token_text(k_param_labels, G_N_ELEMENTS(k_param_labels), param->id));
    gtk_widget_set_size_request(name, 200, -1);
    gtk_grid_attach(GTK_GRID(grid), name, 0, (int)p, 1, 1);
    GtkWidget *spin = gtk_spin_button_new(row->adjustment, 1.0, 0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin),
                                      GTK_UPDATE_IF_VALID);
    gtk_grid_attach(GTK_GRID(grid), spin, 1, (int)p, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), left_label(unit_text(param->unit)), 2,
                    (int)p, 1, 1);
  }

  GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  if (entry->param_count != 0u) {
    GtkWidget *defaults = gtk_button_new_with_label(
        sc_i18n_string_get(SC_I18N_TESTS_BTN_DEFAULTS));
    g_signal_connect(defaults, "clicked", G_CALLBACK(on_defaults_clicked),
                     card);
    gtk_box_append(GTK_BOX(buttons), defaults);
  }
  card->run_button =
      gtk_button_new_with_label(sc_i18n_string_get(SC_I18N_TESTS_BTN_RUN));
  g_signal_connect(card->run_button, "clicked", G_CALLBACK(on_run_clicked),
                   card);
  gtk_box_append(GTK_BOX(buttons), card->run_button);
  gtk_box_append(GTK_BOX(box), buttons);
  return frame;
}

/* "Sequence: Key-value storage, Cyclic ramps, ..." in catalog order. */
static GtkWidget *build_sequence_hint(const ScTestCatalog *catalog) {
  char names[512];
  names[0] = '\0';
  for (size_t i = 0u; i < catalog->count; ++i) {
    if (!catalog->tests[i].in_sequence) {
      continue;
    }
    const size_t used = strlen(names);
    (void)snprintf(&names[used], sizeof(names) - used, "%s%s",
                   (used != 0u) ? ", " : "",
                   test_title(catalog->tests[i].name));
  }
  char text[600];
  (void)snprintf(text, sizeof(text),
                 sc_i18n_string_get(SC_I18N_TESTS_SEQUENCE_LIST_FMT), names);
  return dim_label(text);
}

GtkWidget *sc_tests_tab_build(AppState *state, size_t module_index) {
  TestsTab *t = g_new0(TestsTab, 1);
  t->state = state;
  t->module_index = module_index;

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start(root, 12);
  gtk_widget_set_margin_end(root, 12);
  gtk_widget_set_margin_top(root, 12);
  gtk_widget_set_margin_bottom(root, 12);
  t->root = root;
  g_object_set_data_full(G_OBJECT(root), "sc-tests-tab", t, tests_tab_free);

  char err[512];
  err[0] = '\0';
  if (!sc_tests_load_catalog(&state->core, module_index, &t->catalog, err,
                             sizeof(err))) {
    char text[640];
    (void)snprintf(text, sizeof(text),
                   sc_i18n_string_get(SC_I18N_TESTS_LOAD_FAILED_FMT), err);
    gtk_box_append(GTK_BOX(root), centered_message(text));
    return root;
  }
  if (!t->catalog.supported || t->catalog.count == 0u) {
    gtk_box_append(GTK_BOX(root),
                   centered_message(sc_i18n_string_get(SC_I18N_TESTS_NONE)));
    return root;
  }

  GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  t->sequence_button = gtk_button_new_with_label(
      sc_i18n_string_get(SC_I18N_TESTS_BTN_RUN_SEQUENCE));
  t->skip_button =
      gtk_button_new_with_label(sc_i18n_string_get(SC_I18N_TESTS_BTN_SKIP));
  t->stop_button =
      gtk_button_new_with_label(sc_i18n_string_get(SC_I18N_TESTS_BTN_STOP));
  g_signal_connect(t->sequence_button, "clicked",
                   G_CALLBACK(on_sequence_clicked), t);
  g_signal_connect(t->skip_button, "clicked", G_CALLBACK(on_skip_clicked), t);
  g_signal_connect(t->stop_button, "clicked", G_CALLBACK(on_stop_clicked), t);
  gtk_box_append(GTK_BOX(controls), t->sequence_button);
  gtk_box_append(GTK_BOX(controls), t->skip_button);
  gtk_box_append(GTK_BOX(controls), t->stop_button);
  t->message = left_label("");
  gtk_label_set_wrap(GTK_LABEL(t->message), TRUE);
  gtk_widget_set_hexpand(t->message, TRUE);
  gtk_box_append(GTK_BOX(controls), t->message);
  gtk_box_append(GTK_BOX(root), controls);

  gtk_box_append(GTK_BOX(root), build_sequence_hint(&t->catalog));
  gtk_box_append(GTK_BOX(root), build_status_panel(t));
  gtk_box_append(GTK_BOX(root),
                 dim_label(sc_i18n_string_get(SC_I18N_TESTS_NOTE)));

  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  GtkWidget *cards = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), cards);
  gtk_box_append(GTK_BOX(root), scroll);
  for (size_t i = 0u; i < t->catalog.count; ++i) {
    gtk_box_append(GTK_BOX(cards), build_card(t, &t->catalog.tests[i]));
  }

  ScTestStatus status;
  if (refresh_status(t, &status)) {
    set_message(t, "");
  }
  t->poll_id = g_timeout_add(SC_TESTS_POLL_INTERVAL_MS, on_poll, t);
  return root;
}
