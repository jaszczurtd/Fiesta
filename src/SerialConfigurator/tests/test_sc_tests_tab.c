/*
 * sc_tests_tab: the Tests sub-tab of the ECU and Clocks pages, built against
 * the mock module. Covers what cannot be clicked through on the bench by a
 * test run: the empty-catalog message, one card per reported test, the
 * status panel in the operator's words, and the Run / Run sequence paths
 * (edited parameters first, then the start, refusals shown as text). For
 * Clocks also: no actuator rows, and the shared step time shown and sent
 * once.
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

/* Build the tab for the mock module; the caller owns the returned root. */
static GtkWidget *build_tab(void) {
  memset(&s_state, 0, sizeof(s_state));
  if (!sc_mock_ecu_detected_core(&s_state.core)) {
    return NULL;
  }
  s_state.connected = true;
  GtkWidget *root = sc_tests_tab_build(&s_state, sc_mock_ecu_module_index());
  g_object_ref_sink(root);
  return root;
}

/* Widgets of @p type below @p widget, itself included. */
static unsigned count_widgets(GtkWidget *widget, GType type) {
  unsigned count = G_TYPE_CHECK_INSTANCE_TYPE(widget, type) ? 1u : 0u;
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    count += count_widgets(child, type);
  }
  return count;
}

/* True when a label below @p widget shows exactly @p text. */
static bool has_label(GtkWidget *widget, const char *text) {
  if (GTK_IS_LABEL(widget) &&
      strcmp(gtk_label_get_text(GTK_LABEL(widget)), text) == 0) {
    return true;
  }
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    if (has_label(child, text)) {
      return true;
    }
  }
  return false;
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
  TEST_ASSERT(t->shared.param_count == 0u, "the ECU shares no parameter");
  TEST_ASSERT(!has_label(root, sc_i18n_string_get(SC_I18N_TESTS_SHARED_PARAMS)),
              "no frame for shared parameters");
  TEST_ASSERT(t->has_drive && count_widgets(root, GTK_TYPE_PROGRESS_BAR) == 2u,
              "demand and position rows of the actuator");
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
  /* Keys left out, e.g. with an invalid calibration: the rows stay and say
   * that the readings are missing. */
  TEST_ASSERT_CONTAINS(t->demand_label,
                       sc_i18n_string_get(SC_I18N_TESTS_DRIVE_UNKNOWN),
                       "no drive reading");
  TEST_ASSERT(count_widgets(root, GTK_TYPE_PROGRESS_BAR) == 2u,
              "the actuator rows stay without a reading");
  g_object_unref(root);
  return 0;
}

static int test_clocks_tab_has_five_cards_and_no_actuator_rows(void) {
  sc_mock_ecu_reset();
  s_ecu.clocks = true;
  GtkWidget *root = build_tab();
  TEST_ASSERT(root != NULL, "built");
  TestsTab *t = tab_of(root);
  static const char *const k_names[] = {"charge", "oil", "rpm", "speed",
                                        "buzzer"};
  static const char *const k_titles[] = {"Charge lamp", "Oil pressure lamp",
                                         "Tachometer", "Speedometer", "Buzzer"};
  TEST_ASSERT(t->card_count == 5u, "five cluster tests");
  for (size_t i = 0u; i < 5u; ++i) {
    TEST_ASSERT(strcmp(t->cards[i].name, k_names[i]) == 0, "catalog order");
    TEST_ASSERT(t->cards[i].param_count == 0u, "no parameter of its own");
    TEST_ASSERT(strcmp(test_title(k_names[i]), k_titles[i]) == 0, "title");
  }
  TEST_ASSERT(count_widgets(root, GTK_TYPE_FRAME) == 7u,
              "status, shared parameters and five cards");

  TEST_ASSERT(!t->has_drive, "Clocks reports no actuator");
  TEST_ASSERT(t->demand_label == NULL && t->position_label == NULL &&
                  count_widgets(root, GTK_TYPE_PROGRESS_BAR) == 0u,
              "no demand or position row");
  TEST_ASSERT(!has_label(root, sc_i18n_string_get(SC_I18N_TESTS_DRIVE_UNKNOWN)),
              "no 'unavailable' text either");
  TEST_ASSERT(has_label(root, sc_i18n_string_get(SC_I18N_TESTS_NOTE)) &&
                  strstr(sc_i18n_string_get(SC_I18N_TESTS_NOTE), "ECU") == NULL,
              "the lost-contact note does not name the ECU");

  ScTestStatus status;
  s_ecu.status_reply =
      "SC_OK TEST_STATUS state=running runs=1 test=rpm src=sc elapsed_ms=2100 "
      "seq=3/5 step=2 steps=5 value=3000 last=oil result=done";
  TEST_ASSERT(refresh_status(t, &status), "status read");
  TEST_ASSERT(!status.drive_valid, "status without actuator keys");
  TEST_ASSERT_CONTAINS(t->status_title, "Tachometer", "test title");
  TEST_ASSERT_CONTAINS(t->status_sequence, "test 3 of 5", "sequence step");
  TEST_ASSERT_CONTAINS(t->status_progress, "step 2/5", "step of steps");
  TEST_ASSERT_CONTAINS(t->status_progress, "3000 rpm", "tachometer value");
  TEST_ASSERT(strstr(gtk_label_get_text(GTK_LABEL(t->status_progress)),
                     "steps") == NULL,
              "the step total is not shown twice");
  TEST_ASSERT_CONTAINS(t->status_last, "Oil pressure lamp", "last test");

  s_ecu.status_reply = "SC_OK TEST_STATUS state=running runs=2 test=speed "
                       "src=sc elapsed_ms=10 step=3 steps=7 value=100";
  TEST_ASSERT(refresh_status(t, &status), "status read");
  TEST_ASSERT_CONTAINS(t->status_progress, "100 km/h", "speedometer value");

  s_ecu.status_reply = "SC_OK TEST_STATUS state=running runs=3 test=charge "
                       "src=sc elapsed_ms=10 step=3 steps=3 phase=blink";
  TEST_ASSERT(refresh_status(t, &status), "status read");
  TEST_ASSERT_CONTAINS(t->status_progress, "lamp blinking", "lamp step");
  TEST_ASSERT(strstr(gtk_label_get_text(GTK_LABEL(t->status_progress)),
                     "phase") == NULL,
              "a step name reads on its own");

  s_ecu.status_reply = "SC_OK TEST_STATUS state=running runs=4 test=buzzer "
                       "src=sc elapsed_ms=10 step=3 steps=3 phase=long";
  TEST_ASSERT(refresh_status(t, &status), "status read");
  TEST_ASSERT_CONTAINS(t->status_progress, "long signal", "buzzer step");
  g_object_unref(root);
  return 0;
}

static int test_shared_parameter_is_shown_and_sent_once(void) {
  sc_mock_ecu_reset();
  s_ecu.clocks = true;
  GtkWidget *root = build_tab();
  TEST_ASSERT(root != NULL, "built");
  TestsTab *t = tab_of(root);
  TEST_ASSERT(t->shared.param_count == 1u && t->param_count == 1u,
              "one row for the step time of five tests");
  TEST_ASSERT(strcmp(t->params[0].id, "step_ms") == 0, "the step time");
  TEST_ASSERT(has_label(root, sc_i18n_string_get(SC_I18N_TESTS_SHARED_PARAMS)),
              "its own frame");
  TEST_ASSERT(has_label(root, sc_i18n_string_get(SC_I18N_TEST_PARAM_STEP)) &&
                  has_label(root, "ms"),
              "name and unit");
  TEST_ASSERT(gtk_adjustment_get_lower(t->params[0].adjustment) == 1200.0 &&
                  gtk_adjustment_get_upper(t->params[0].adjustment) == 10000.0,
              "range from the module");

  /* Any card sends the edited value before its start. */
  gtk_adjustment_set_value(t->params[0].adjustment, 3000.0);
  on_run_clicked(NULL, &t->cards[2]);
  TEST_ASSERT(strcmp(s_ecu.last_set, "step_ms 3000") == 0 && s_ecu.sets == 1u,
              "edited step time sent");
  TEST_ASSERT(strcmp(s_ecu.last_run, "rpm") == 0, "test started");
  TEST_ASSERT_CONTAINS(t->message, "Started: Tachometer", "feedback");

  /* The next card and the sequence find it already in force. */
  on_run_clicked(NULL, &t->cards[3]);
  TEST_ASSERT(strcmp(s_ecu.last_run, "speed") == 0 && s_ecu.sets == 1u,
              "not sent again");
  on_sequence_clicked(NULL, t);
  TEST_ASSERT(strcmp(s_ecu.last_run, SC_TEST_SEQUENCE) == 0 && s_ecu.sets == 1u,
              "sequence without a second write");

  /* The sequence sends it too when it changed. */
  on_defaults_clicked(NULL, &t->shared);
  TEST_ASSERT(gtk_adjustment_get_value(t->params[0].adjustment) == 2000.0,
              "default restored in the form");
  on_sequence_clicked(NULL, t);
  TEST_ASSERT(strcmp(s_ecu.last_set, "step_ms 2000") == 0 && s_ecu.sets == 2u,
              "sequence sends the changed step time");

  /* A refused value stops the start. */
  s_ecu.last_run[0] = '\0';
  gtk_adjustment_set_value(t->params[0].adjustment, 5000.0);
  s_ecu.clocks = false; /* the module now refuses the id */
  on_run_clicked(NULL, &t->cards[0]);
  s_ecu.clocks = true;
  TEST_ASSERT(s_ecu.last_run[0] == '\0' && s_ecu.sets == 2u,
              "no start after a refused parameter");
  TEST_ASSERT_CONTAINS(t->message, sc_i18n_string_get(SC_I18N_TEST_PARAM_STEP),
                       "the refusal names the parameter");
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
  /* The sequence sends every card's edited parameters. */
  on_sequence_clicked(NULL, t);
  TEST_ASSERT(strcmp(s_ecu.last_set, "cyclic_passes 4") == 0,
              "sequence sends the restored default");
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
    return SC_TEST_SKIPPED;
  }
  if (!gtk_init_check()) {
    fprintf(stdout, "test_sc_tests_tab: skipped "
                    "(gtk_init_check failed - no usable display)\n");
    return SC_TEST_SKIPPED;
  }
  /* gtk_init takes the system locale; numbers are checked with a dot. */
  (void)setlocale(LC_NUMERIC, "C");
  sc_i18n_set_locale(SC_LOCALE_EN);

  int failures = 0;
  failures += test_production_and_older_firmware_show_no_tests();
  failures += test_catalog_gives_one_card_per_test();
  failures += test_status_panel_speaks_the_operator_language();
  failures += test_clocks_tab_has_five_cards_and_no_actuator_rows();
  failures += test_shared_parameter_is_shown_and_sent_once();
  failures += test_run_sends_edited_parameters_then_starts();
  failures += test_refusals_are_explained();
  if (failures != 0) {
    fprintf(stderr, "test_sc_tests_tab: %d test(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  fprintf(stdout, "test_sc_tests_tab: all tests passed\n");
  return EXIT_SUCCESS;
}
