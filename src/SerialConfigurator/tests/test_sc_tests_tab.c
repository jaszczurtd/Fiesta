/*
 * sc_tests_tab: the Tests sub-tab of the ECU page, built against the mock
 * ECU. Covers what cannot be clicked through on the bench by a test run:
 * the empty-catalog message, one card per reported test, the status panel
 * in the operator's words, and the Run / Run sequence paths (edited
 * parameters first, then the start, refusals shown as text).
 *
 * Like test_sc_progressbar.c it needs a display and skips without one; the
 * widgets are never realised. The source is embedded by #include so the
 * test can reach the tab state behind the root widget.
 */

#include "../src/ui/sc_tests_tab.c"

#include "sc_mock_ecu.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg)                                                 \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL: %s - %s (line %d)\n", __func__, (msg), __LINE__); \
      return 1;                                                                \
    }                                                                          \
  } while (0)

#define TEST_ASSERT_CONTAINS(label, text, msg)                                 \
  do {                                                                         \
    const char *got_ = gtk_label_get_text(GTK_LABEL(label));                   \
    if (strstr(got_, (text)) == NULL) {                                        \
      fprintf(stderr, "FAIL: %s - %s (line %d): '%s' lacks '%s'\n", __func__,  \
              (msg), __LINE__, got_, (text));                                  \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static AppState s_state;

/* Build the tab for the mock ECU; the caller owns the returned root. */
static GtkWidget *build_tab(void) {
  memset(&s_state, 0, sizeof(s_state));
  if (!sc_mock_ecu_detected_core(&s_state.core)) {
    return NULL;
  }
  s_state.connected = true;
  GtkWidget *root = sc_tests_tab_build(&s_state, 0u);
  g_object_ref_sink(root);
  return root;
}

static TestsTab *tab_of(GtkWidget *root) {
  return (TestsTab *)g_object_get_data(G_OBJECT(root), "sc-tests-tab");
}

static int expect_only_message(GtkWidget *root, ScI18nKey key) {
  GtkWidget *child = gtk_widget_get_first_child(root);
  TEST_ASSERT(child != NULL && GTK_IS_LABEL(child), "one message label");
  TEST_ASSERT(gtk_widget_get_next_sibling(child) == NULL, "nothing else");
  TEST_ASSERT(strcmp(gtk_label_get_text(GTK_LABEL(child)),
                     sc_i18n_string_get(key)) == 0,
              "message text");
  TEST_ASSERT(gtk_widget_get_halign(child) == GTK_ALIGN_CENTER &&
                  gtk_widget_get_valign(child) == GTK_ALIGN_CENTER,
              "centered");
  TEST_ASSERT(tab_of(root)->poll_id == 0u, "no polling without tests");
  return 0;
}

static int test_production_and_older_firmware_show_no_tests(void) {
  sc_mock_ecu_reset();
  s_ecu.production = true;
  GtkWidget *root = build_tab();
  TEST_ASSERT(root != NULL, "built");
  const int failed = expect_only_message(root, SC_I18N_TESTS_NONE);
  g_object_unref(root);
  if (failed != 0) {
    return failed;
  }

  sc_mock_ecu_reset();
  s_ecu.tests_compiled = false;
  root = build_tab();
  TEST_ASSERT(root != NULL, "built");
  const int failed_old = expect_only_message(root, SC_I18N_TESTS_NONE);
  g_object_unref(root);
  return failed_old;
}

static int test_catalog_gives_one_card_per_test(void) {
  sc_mock_ecu_reset();
  GtkWidget *root = build_tab();
  TEST_ASSERT(root != NULL, "built");
  TestsTab *t = tab_of(root);
  TEST_ASSERT(t->card_count == 2u, "pot and cyclic");
  TEST_ASSERT(strcmp(t->cards[1].name, "cyclic") == 0, "second card");
  TEST_ASSERT(t->cards[1].first_param == 0u && t->cards[1].param_count == 2u,
              "cyclic owns both parameters");
  TEST_ASSERT(t->param_count == 2u, "two parameter rows");
  TEST_ASSERT(gtk_adjustment_get_value(t->params[0].adjustment) == 4.0,
              "value in force");
  TEST_ASSERT(gtk_adjustment_get_upper(t->params[0].adjustment) == 100.0,
              "range from the ECU");
  TEST_ASSERT_CONTAINS(t->status_title, sc_i18n_string_get(SC_I18N_TESTS_IDLE),
                       "idle");
  TEST_ASSERT(!gtk_widget_get_sensitive(t->stop_button), "nothing to stop");
  TEST_ASSERT(gtk_widget_get_sensitive(t->cards[0].run_button), "can run");
  TEST_ASSERT(t->poll_id != 0u, "polling armed");

  const guint poll_id = t->poll_id;
  g_object_unref(root);
  TEST_ASSERT(g_main_context_find_source_by_id(NULL, poll_id) == NULL,
              "poll timer goes with the tab");
  return 0;
}

static int test_status_panel_speaks_the_operator_language(void) {
  sc_mock_ecu_reset();
  GtkWidget *root = build_tab();
  TEST_ASSERT(root != NULL, "built");
  TestsTab *t = tab_of(root);
  s_ecu.status_reply =
      "SC_OK TEST_STATUS state=running runs=4 test=cyclic src=console "
      "elapsed_ms=12300 seq=2/5 demand_x10=905 position_x10=887 profile=2 "
      "rate=166 cycle=3 cycles=6 pass=1 passes=4 last=pot result=host_lost";
  ScTestStatus status;
  TEST_ASSERT(refresh_status(t, &status), "status read");

  TEST_ASSERT_CONTAINS(t->status_title, "Cyclic ramps", "test title");
  TEST_ASSERT_CONTAINS(t->status_title, "started from the console", "source");
  TEST_ASSERT_CONTAINS(t->status_sequence, "test 2 of 5", "sequence step");
  TEST_ASSERT_CONTAINS(t->status_elapsed, "12.3 s", "elapsed");
  TEST_ASSERT_CONTAINS(t->demand_label, "90.5 %", "demand");
  TEST_ASSERT_CONTAINS(t->position_label, "88.7 %", "position");
  TEST_ASSERT(gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(t->position_bar)) >
                  0.88,
              "position bar");
  TEST_ASSERT_CONTAINS(t->status_progress, "profile 2", "profile");
  TEST_ASSERT_CONTAINS(t->status_progress, "166 %/s", "rate");
  TEST_ASSERT_CONTAINS(t->status_progress, "cycle 3/6", "cycle of cycles");
  TEST_ASSERT_CONTAINS(t->status_progress, "pass 1/4", "pass of passes");
  TEST_ASSERT(strstr(gtk_label_get_text(GTK_LABEL(t->status_progress)),
                     "cycles") == NULL,
              "totals are not shown twice");
  TEST_ASSERT_CONTAINS(t->status_last, "Pot turn", "last test");
  TEST_ASSERT_CONTAINS(t->status_last, "contact with the configurator lost",
                       "last result");
  TEST_ASSERT(gtk_widget_get_sensitive(t->stop_button), "can stop");
  TEST_ASSERT(!gtk_widget_get_sensitive(t->sequence_button),
              "no second start while one runs");

  s_ecu.status_reply = "SC_OK TEST_STATUS state=running runs=5 test=pot "
                       "src=sc elapsed_ms=100 phase=hold rate=250 pass=1 "
                       "passes=3";
  TEST_ASSERT(refresh_status(t, &status), "status read");
  TEST_ASSERT_CONTAINS(t->status_progress, "phase: holding", "pot phase");
  TEST_ASSERT_CONTAINS(t->demand_label,
                       sc_i18n_string_get(SC_I18N_TESTS_DRIVE_UNKNOWN),
                       "no drive reading");
  g_object_unref(root);
  return 0;
}

static int test_run_sends_edited_parameters_then_starts(void) {
  sc_mock_ecu_reset();
  GtkWidget *root = build_tab();
  TEST_ASSERT(root != NULL, "built");
  TestsTab *t = tab_of(root);

  gtk_adjustment_set_value(t->params[0].adjustment, 7.0);
  on_run_clicked(NULL, &t->cards[1]);
  TEST_ASSERT(strcmp(s_ecu.last_set, "cyclic_passes 7") == 0,
              "edited parameter sent");
  TEST_ASSERT(strcmp(s_ecu.last_run, "cyclic") == 0, "test started");
  TEST_ASSERT(t->owns_test, "the tab keeps polling for its test");
  TEST_ASSERT(t->params[0].applied == 7, "value now in force");
  TEST_ASSERT_CONTAINS(t->message, "Started: Cyclic ramps", "feedback");

  /* A later start that has finished ends the ownership. */
  s_ecu.status_reply =
      "SC_OK TEST_STATUS state=idle runs=1 last=cyclic result=done";
  ScTestStatus status;
  TEST_ASSERT(refresh_status(t, &status), "status read");
  TEST_ASSERT(!t->owns_test, "finished");

  /* Unchanged parameters are not sent again. */
  s_ecu.last_set[0] = '\0';
  on_sequence_clicked(NULL, t);
  TEST_ASSERT(s_ecu.last_set[0] == '\0', "nothing edited");
  TEST_ASSERT(strcmp(s_ecu.last_run, SC_TEST_SEQUENCE) == 0, "sequence");

  on_defaults_clicked(NULL, &t->cards[1]);
  TEST_ASSERT(gtk_adjustment_get_value(t->params[0].adjustment) == 4.0,
              "defaults restored in the form");
  g_object_unref(root);
  return 0;
}

static int test_refusals_are_explained(void) {
  sc_mock_ecu_reset();
  GtkWidget *root = build_tab();
  TEST_ASSERT(root != NULL, "built");
  TestsTab *t = tab_of(root);

  s_ecu.engine_running = true;
  on_sequence_clicked(NULL, t);
  TEST_ASSERT_CONTAINS(t->message, "the engine is running", "interlock");
  TEST_ASSERT(!t->owns_test, "nothing started");
  s_ecu.engine_running = false;

  s_ecu.busy_replies = SC_TESTS_BUSY_RETRIES + 1u;
  on_stop_clicked(NULL, t);
  TEST_ASSERT_CONTAINS(t->message, "busy", "busy after retries");
  s_ecu.busy_replies = 0u;
  on_skip_clicked(NULL, t);
  TEST_ASSERT_CONTAINS(
      t->message, sc_i18n_string_get(SC_I18N_TESTS_SKIP_REQUESTED), "skip");
  g_object_unref(root);
  return 0;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  const char *display = g_getenv("DISPLAY");
  const char *wayland = g_getenv("WAYLAND_DISPLAY");
  if ((display == NULL || display[0] == '\0') &&
      (wayland == NULL || wayland[0] == '\0')) {
    fprintf(stdout, "test_sc_tests_tab: skipped "
                    "(no DISPLAY / WAYLAND_DISPLAY in env)\n");
    return EXIT_SUCCESS;
  }
  if (!gtk_init_check()) {
    fprintf(stdout, "test_sc_tests_tab: skipped "
                    "(gtk_init_check failed - no usable display)\n");
    return EXIT_SUCCESS;
  }
  /* gtk_init takes the system locale; numbers are checked with a dot. */
  (void)setlocale(LC_NUMERIC, "C");
  sc_i18n_set_locale(SC_LOCALE_EN);

  int failures = 0;
  failures += test_production_and_older_firmware_show_no_tests();
  failures += test_catalog_gives_one_card_per_test();
  failures += test_status_panel_speaks_the_operator_language();
  failures += test_run_sends_edited_parameters_then_starts();
  failures += test_refusals_are_explained();
  if (failures != 0) {
    fprintf(stderr, "test_sc_tests_tab: %d test(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  fprintf(stdout, "test_sc_tests_tab: all tests passed\n");
  return EXIT_SUCCESS;
}
