/* Production Values and Map polling against a fake Clocks transport, the
 * RTC_Clock date and time form against a fake RTC_Clock, and the session
 * end when the window closes. */
#include "../src/ui/sc_detection.c"
#include "../src/ui/sc_map_tab.c"
#include "../src/ui/sc_values_tab.c"
#undef NDEBUG
#include <assert.h>
#include <locale.h>
#include <time.h>

/* sc_detection.c updates the module list, which is not under test here. */
void sc_modules_view_set_meta_status(AppState *s, size_t i, const char *m) {
  (void)s, (void)i, (void)m;
}
void sc_modules_view_set_catalog_status(AppState *s, size_t i, const char *m) {
  (void)s, (void)i, (void)m;
}
void sc_modules_view_set_values_status(AppState *s, size_t i, const char *m) {
  (void)s, (void)i, (void)m;
}
void sc_modules_view_set_param_probe_status(AppState *s, size_t i,
                                            const char *m) {
  (void)s, (void)i, (void)m;
}
void sc_modules_view_set_placeholder(AppState *s, const char *m) {
  (void)s, (void)m;
}
void sc_modules_view_refresh_lamps(AppState *s) { (void)s; }
void sc_modules_view_refresh_details(AppState *s) { (void)s; }
void sc_modules_view_select_first_detected(AppState *s) { (void)s; }

static unsigned s_tests_tabs_built;
static size_t s_tests_tab_index;

GtkWidget *sc_tests_tab_build(AppState *state, size_t index) {
  (void)state;
  ++s_tests_tabs_built;
  s_tests_tab_index = index;
  return gtk_label_new("");
}

static const char *tab_text(GtkWidget *notebook, int page) {
  return gtk_notebook_get_tab_label_text(
      GTK_NOTEBOOK(notebook),
      gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), page));
}

typedef struct {
  bool fail, no_fix;
  unsigned calls;
  int oil, abs, egt0, egt1;
  unsigned valid;
} FakeClocks;

static bool send_command(void *user, const char *path, const char *command,
                         char *response, size_t size, char *error,
                         size_t error_size) {
  (void)error;
  (void)error_size;
  FakeClocks *fake = user;
  assert(strcmp(path, "/dev/clocks") == 0);
  ++fake->calls;
  if (fake->fail)
    return false;
  if (strcmp(command, "SC_GET_PARAM_LIST") == 0) {
    snprintf(response, size,
             "SC_OK PARAM_LIST oil_cbar,abs_kmh,egt0_c,egt1_c,valid");
  } else if (strncmp(command, "SC_GET_PARAM ", 13u) == 0) {
    snprintf(response, size,
             "SC_OK PARAM id=%s value=0 min=0 max=32767 default=0 "
             "group=%s",
             command + 13u,
             strcmp(command + 13u, "valid") == 0 ? "validity" : "measurements");
  } else if (strcmp(command, "SC_GET_VALUES") == 0) {
    snprintf(response, size,
             "SC_OK PARAM_VALUES oil_cbar=%d abs_kmh=%d egt0_c=%d egt1_c=%d "
             "valid=%u",
             fake->oil, fake->abs, fake->egt0, fake->egt1, fake->valid);
  } else if (strcmp(command, "SC_GET_GPS") == 0) {
    snprintf(response, size,
             "SC_OK GPS available=%d lat_e6=52229676 lon_e6=21012229 "
             "speed_kmh_x10=123 epoch=1767268800",
             !fake->no_fix);
  } else {
    assert(false);
  }
  return true;
}

static void expect_reading(ModuleSubtab *module, unsigned index,
                           const char *text) {
  ParamRow *row = g_ptr_array_index(module->rows, index);
  assert(row->adjustment == NULL);
  assert(strcmp(gtk_label_get_text(GTK_LABEL(row->read_label)), text) == 0);
}

/* ── RTC_Clock date and time form against a fake RTC_Clock ────────── */

#define RTC_PATH "/dev/rtc_clock"

/* Answers as Fiesta_clock config.c does: staging follows the running clock
 * until a SET_PARAM, COMMIT checks the whole date and sets the clock. */
typedef struct {
  int active[6];
  int staging[6];
  int integrity;
  bool dirty, authenticated;
  bool reject_auth, fail_values, fail_values_after_commit, fail_revert;
  const char *reject_id;     /* SET_PARAM answered out_of_range */
  const char *commit_reason; /* COMMIT answered SC_COMMIT_FAILED */
  char log[1024];            /* one line per command, in order */
} FakeRtc;

static FakeRtc s_rtc;
static const char *const k_fake_rtc_ids[6] = {
    SC_RTC_YEAR_ID, SC_RTC_MONTH_ID,  SC_RTC_DAY_ID,
    SC_RTC_HOUR_ID, SC_RTC_MINUTE_ID, SC_RTC_SECOND_ID};
static const int k_fake_rtc_min[6] = {2000, 1, 1, 0, 0, 0};
static const int k_fake_rtc_max[6] = {2099, 12, 31, 23, 59, 59};

static void rtc_log(const char *line) {
  const size_t used = strlen(s_rtc.log);
  snprintf(s_rtc.log + used, sizeof(s_rtc.log) - used, "%s\n", line);
}

static int rtc_fake_field(const char *id) {
  for (int i = 0; i < 6; ++i) {
    if (strcmp(k_fake_rtc_ids[i], id) == 0) {
      return i;
    }
  }
  assert(false);
  return 0;
}

static bool rtc_fake_valid(const int *v) {
  static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  for (int i = 0; i < 6; ++i) {
    if (v[i] < k_fake_rtc_min[i] || v[i] > k_fake_rtc_max[i]) {
      return false;
    }
  }
  const bool leap = v[0] % 4 == 0 && (v[0] % 100 != 0 || v[0] % 400 == 0);
  return v[2] <= days[v[1] - 1] + (v[1] == 2 && leap ? 1 : 0);
}

static void rtc_fake_refresh(void) {
  if (!s_rtc.dirty) {
    memcpy(s_rtc.staging, s_rtc.active, sizeof(s_rtc.staging));
  }
}

static bool rtc_hello(void *user, const char *path, char *response, size_t size,
                      char *error, size_t error_size) {
  (void)user;
  (void)error;
  (void)error_size;
  assert(strcmp(path, RTC_PATH) == 0);
  rtc_log(SC_CMD_HELLO);
  s_rtc.authenticated = false;
  snprintf(response, size,
           "OK HELLO module=%s proto=1 session=7 fw=v1 build=b1 "
           "uid=E661A4D1234567AB",
           SC_MODULE_TOKEN_CLOCK);
  return true;
}

static bool rtc_command(void *user, const char *path, const char *command,
                        char *response, size_t size, char *error,
                        size_t error_size) {
  (void)user;
  assert(strcmp(path, RTC_PATH) == 0);
  const bool prove = strncmp(command, SC_CMD_AUTH_PROVE " ",
                             strlen(SC_CMD_AUTH_PROVE " ")) == 0;
  rtc_log(prove ? SC_CMD_AUTH_PROVE : command);
  char id[SC_PARAM_ID_MAX] = {0};
  int value = 0;
  const int *a = s_rtc.active;
  if (strcmp(command, SC_CMD_GET_PARAM_LIST) == 0) {
    snprintf(response, size,
             "SC_OK PARAM_LIST rtc_year,rtc_month,rtc_day,rtc_hour,"
             "rtc_minute,rtc_second,rtc_integrity");
  } else if (sscanf(command, SC_CMD_GET_PARAM " %31s", id) == 1) {
    rtc_fake_refresh();
    const int i = rtc_fake_field(id);
    snprintf(response, size,
             "SC_OK PARAM id=%s value=%d min=%d max=%d default=%d group=rtc",
             id, a[i], k_fake_rtc_min[i], k_fake_rtc_max[i], k_fake_rtc_min[i]);
  } else if (strcmp(command, SC_CMD_GET_VALUES) == 0) {
    if (s_rtc.fail_values) {
      snprintf(error, error_size, "no reply");
      return false;
    }
    rtc_fake_refresh();
    snprintf(response, size,
             "SC_OK PARAM_VALUES rtc_year=%d rtc_month=%d rtc_day=%d "
             "rtc_hour=%d rtc_minute=%d rtc_second=%d rtc_integrity=%d",
             a[0], a[1], a[2], a[3], a[4], a[5], s_rtc.integrity);
  } else if (strcmp(command, SC_CMD_BYE) == 0) {
    snprintf(response, size, "%s", SC_REPLY_BYE_OK);
  } else if (strcmp(command, SC_CMD_AUTH_BEGIN) == 0) {
    snprintf(response, size,
             "SC_OK AUTH_CHALLENGE 101112131415161718191a1b1c1d1e1f");
  } else if (prove) {
    s_rtc.authenticated = !s_rtc.reject_auth;
    snprintf(response, size, "%s",
             s_rtc.authenticated ? SC_REPLY_AUTH_OK : SC_STATUS_NOT_AUTHORIZED);
  } else if (!s_rtc.authenticated) {
    snprintf(response, size, "%s", SC_STATUS_NOT_AUTHORIZED);
  } else if (sscanf(command, SC_CMD_SET_PARAM " %31s %d", id, &value) == 2) {
    const int i = rtc_fake_field(id);
    if (s_rtc.reject_id != NULL && strcmp(id, s_rtc.reject_id) == 0) {
      snprintf(response, size, SC_REPLY_BAD_REQUEST_OUT_OF_RANGE_FMT, id,
               k_fake_rtc_min[i], k_fake_rtc_max[i]);
    } else {
      s_rtc.staging[i] = value;
      s_rtc.dirty = true;
      snprintf(response, size, SC_REPLY_PARAM_SET_FMT, id, value, a[i]);
    }
  } else if (strcmp(command, SC_CMD_COMMIT_PARAMS) == 0) {
    if (s_rtc.commit_reason != NULL || !rtc_fake_valid(s_rtc.staging)) {
      snprintf(response, size, SC_REPLY_COMMIT_FAILED_FMT,
               s_rtc.commit_reason != NULL ? s_rtc.commit_reason
                                           : "invalid_datetime");
    } else {
      memcpy(s_rtc.active, s_rtc.staging, sizeof(s_rtc.active));
      s_rtc.dirty = false;
      s_rtc.integrity = 1;
      s_rtc.fail_values = s_rtc.fail_values_after_commit;
      snprintf(response, size, SC_REPLY_PARAMS_COMMITTED_FMT, 6u);
    }
  } else if (strcmp(command, SC_CMD_REVERT_PARAMS) == 0) {
    if (s_rtc.fail_revert) {
      snprintf(error, error_size, "port closed");
      return false;
    }
    memcpy(s_rtc.staging, s_rtc.active, sizeof(s_rtc.staging));
    s_rtc.dirty = false;
    snprintf(response, size, "%s", SC_REPLY_PARAMS_REVERTED);
  } else {
    assert(false);
  }
  return true;
}

static unsigned count_widgets(GtkWidget *widget, GType type) {
  unsigned count = G_TYPE_CHECK_INSTANCE_TYPE(widget, type) ? 1u : 0u;
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    count += count_widgets(child, type);
  }
  return count;
}

static bool has_text(GtkWidget *widget, const char *text) {
  if (GTK_IS_LABEL(widget) &&
      strcmp(gtk_label_get_text(GTK_LABEL(widget)), text) == 0) {
    return true;
  }
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    if (has_text(child, text)) {
      return true;
    }
  }
  return false;
}

/* A day picked in the calendar, as a click creates it: in the host zone. */
static void pick_date(const RtcForm *form, int year, int month, int day) {
  GDateTime *date = g_date_time_new_local(year, month, day, 0, 0, 0.0);
  calendar_set_date(GTK_CALENDAR(form->calendar), date);
  g_date_time_unref(date);
}

/* The header arrows of a calendar in widget order: previous and next
 * month, then previous and next year. */
static void calendar_arrows(GtkWidget *widget, GtkWidget **arrows,
                            unsigned *count) {
  if (GTK_IS_BUTTON(widget)) {
    if (*count < 4u) {
      arrows[*count] = widget;
    }
    ++*count;
    return;
  }
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    calendar_arrows(child, arrows, count);
  }
}

/* The calendar date and the two-digit time the operator sees. */
static void expect_rtc_form(const RtcForm *form, int year, int month, int day,
                            const char *time_text) {
  GDateTime *date = gtk_calendar_get_date(GTK_CALENDAR(form->calendar));
  int y = 0, m = 0, d = 0;
  g_date_time_get_ymd(date, &y, &m, &d);
  g_date_time_unref(date);
  if (y != year || m != month || d != day) {
    fprintf(stderr, "FAIL: calendar %04d-%02d-%02d, want %04d-%02d-%02d\n", y,
            m, d, year, month, day);
    assert(false);
  }
  char shown[16];
  snprintf(shown, sizeof(shown), "%s:%s:%s",
           gtk_editable_get_text(GTK_EDITABLE(form->time[0])),
           gtk_editable_get_text(GTK_EDITABLE(form->time[1])),
           gtk_editable_get_text(GTK_EDITABLE(form->time[2])));
  if (strcmp(shown, time_text) != 0) {
    fprintf(stderr, "FAIL: time %s, want %s\n", shown, time_text);
    assert(false);
  }
}

static void expect_text(GtkWidget *label, const char *text) {
  const char *shown = gtk_label_get_text(GTK_LABEL(label));
  if (strcmp(shown, text) != 0) {
    fprintf(stderr, "FAIL: '%s', want '%s'\n", shown, text);
    assert(false);
  }
}

static void click(GtkWidget *button) {
  s_rtc.log[0] = '\0';
  g_signal_emit_by_name(button, "clicked");
}

/* Closing the window ends the module sessions with SC_BYE; while flashing
 * or detection uses the ports, the window waits for it to finish. */
static void test_window_close_ends_sessions(AppState *state) {
  const ScTransport saved_transport = state->core.transport;
  static const ScTransportOps ops = {.send_hello = rtc_hello,
                                     .send_sc_command = rtc_command};
  sc_transport_init_custom(&state->core.transport, &ops, NULL);
  memset(&s_rtc, 0, sizeof(s_rtc));
  const bool clocks_detected = state->core.modules[1].detected;
  state->core.modules[1].detected = false; /* only the fake RTC_Clock */
  state->core.modules[3].detected = true;
  snprintf(state->core.modules[3].port_path,
           sizeof(state->core.modules[3].port_path), RTC_PATH);

  for (int busy = 0; busy < 2; ++busy) {
    GtkWidget *window = gtk_window_new();
    g_object_ref(window);
    state->window = window;
    state->connected = true;
    g_signal_connect(window, "close-request",
                     G_CALLBACK(sc_detection_on_close_request), state);
    gtk_window_present(GTK_WINDOW(window));
    s_rtc.log[0] = '\0';
    if (busy == 1) {
      state->flash_in_progress = true;
      gtk_window_close(GTK_WINDOW(window));
      assert(gtk_widget_get_visible(window) && state->close_pending);
      assert(s_rtc.log[0] == '\0');
      sc_detection_close_if_pending(state); /* still flashing */
      assert(gtk_widget_get_visible(window) && s_rtc.log[0] == '\0');
      state->flash_in_progress = false;
      sc_detection_close_if_pending(state);
    } else {
      gtk_window_close(GTK_WINDOW(window));
    }
    assert(!gtk_widget_get_visible(window));
    assert(strcmp(s_rtc.log, SC_CMD_BYE "\n") == 0);
    g_object_unref(window);
    state->close_pending = false;
  }

  /* Without a connection there is no session to end. */
  GtkWidget *window = gtk_window_new();
  g_object_ref(window);
  state->window = window;
  state->connected = false;
  g_signal_connect(window, "close-request",
                   G_CALLBACK(sc_detection_on_close_request), state);
  gtk_window_present(GTK_WINDOW(window));
  s_rtc.log[0] = '\0';
  gtk_window_close(GTK_WINDOW(window));
  assert(!gtk_widget_get_visible(window) && s_rtc.log[0] == '\0');
  g_object_unref(window);

  state->window = NULL;
  state->core.modules[3].detected = false;
  state->core.modules[1].detected = clocks_detected;
  state->core.transport = saved_transport;
  puts("PASS: closing the window ends the sessions, after flashing too");
}

static void test_rtc_form(AppState *state) {
  /* Far from CET/CEST: a date or hour taken through the host zone shows. */
  char *saved_tz = g_strdup(g_getenv("TZ"));
  g_setenv("TZ", "KIR-14", TRUE); /* POSIX form, UTC+14 without tzdata */
  tzset();
  GTimeZone *zone = g_time_zone_new_local();
  GDateTime *now = g_date_time_new_now(zone);
  assert(g_date_time_get_utc_offset(now) == 14 * G_TIME_SPAN_HOUR);
  g_date_time_unref(now);
  g_time_zone_unref(zone);
  const ScTransport saved_transport = state->core.transport;
  static const ScTransportOps ops = {.send_hello = rtc_hello,
                                     .send_sc_command = rtc_command};
  sc_transport_init_custom(&state->core.transport, &ops, NULL);
  memset(&s_rtc, 0, sizeof(s_rtc));
  const int start[6] = {2026, 11, 17, 23, 59, 50};
  memcpy(s_rtc.active, start, sizeof(start));
  state->connected = true;
  state->core.modules[3].detected = true;
  snprintf(state->core.modules[3].port_path,
           sizeof(state->core.modules[3].port_path), RTC_PATH);

  GtkWidget *page = build_module_subtab(state, 3u);
  g_object_ref_sink(page);
  ModuleSubtab *m = g_object_get_data(G_OBJECT(page), "sc-values-subtab");
  RtcForm *f = m->rtc;
  assert(f != NULL && m->rows->len == 0u);
  /* Metadata for the bounds, then the whole date and time in one read. */
  assert(strcmp(s_rtc.log, "SC_GET_PARAM_LIST\n"
                           "SC_GET_PARAM rtc_year\n"
                           "SC_GET_PARAM rtc_month\n"
                           "SC_GET_PARAM rtc_day\n"
                           "SC_GET_PARAM rtc_hour\n"
                           "SC_GET_PARAM rtc_minute\n"
                           "SC_GET_PARAM rtc_second\n"
                           "SC_GET_VALUES\n") == 0);
  /* One calendar and three spin buttons; no sliders, no per-value status. */
  assert(count_widgets(page, GTK_TYPE_SCALE) == 0u);
  assert(count_widgets(page, GTK_TYPE_CALENDAR) == 1u);
  assert(count_widgets(page, GTK_TYPE_SPIN_BUTTON) == 3u);
  assert(strcmp(gtk_button_get_label(GTK_BUTTON(f->set_button)),
                sc_i18n_string_get(SC_I18N_RTC_BTN_SET)) == 0);
  assert(strcmp(gtk_button_get_label(GTK_BUTTON(f->read_button)),
                sc_i18n_string_get(SC_I18N_RTC_BTN_READ)) == 0);
  expect_rtc_form(f, 2026, 11, 17, "23:59:50");
  expect_text(f->state_label, sc_i18n_string_get(SC_I18N_RTC_STATE_UNTRUSTED));
  expect_text(m->footer_status, "");
  assert(gtk_widget_get_sensitive(f->set_button));
  char text[512];
  snprintf(text, sizeof(text), sc_i18n_string_get(SC_I18N_RTC_YEARS_FMT), 2000,
           2099);
  assert(has_text(page, text));
  assert(has_text(page, sc_i18n_string_get(SC_I18N_RTC_LOCAL_TIME)));
  puts("PASS: RTC form shows one snapshot, the device years and the state");

  /* Leaving the device years, by a picked day or by the header arrows at
   * either end, moves to the nearest day within them. */
  pick_date(f, 2100, 3, 15);
  expect_rtc_form(f, 2099, 12, 31, "23:59:50");
  pick_date(f, 1999, 6, 15);
  expect_rtc_form(f, 2000, 1, 1, "23:59:50");
  GtkWidget *arrows[4] = {NULL};
  unsigned arrow_count = 0u;
  calendar_arrows(f->calendar, arrows, &arrow_count);
  assert(arrow_count == 4u);
  pick_date(f, 2099, 12, 15);
  g_signal_emit_by_name(arrows[1], "clicked"); /* next month */
  expect_rtc_form(f, 2099, 12, 31, "23:59:50");
  pick_date(f, 2099, 6, 15);
  g_signal_emit_by_name(arrows[3], "clicked"); /* next year */
  expect_rtc_form(f, 2099, 12, 31, "23:59:50");
  pick_date(f, 2000, 1, 15);
  g_signal_emit_by_name(arrows[0], "clicked"); /* previous month */
  expect_rtc_form(f, 2000, 1, 1, "23:59:50");
  pick_date(f, 2000, 6, 15);
  g_signal_emit_by_name(arrows[2], "clicked"); /* previous year */
  expect_rtc_form(f, 2000, 1, 1, "23:59:50");
  /* Within the years the arrows move as usual. */
  g_signal_emit_by_name(arrows[1], "clicked");
  g_signal_emit_by_name(arrows[3], "clicked");
  expect_rtc_form(f, 2001, 2, 1, "23:59:50");
  /* A typed time outside the range is refused and stays for correction;
   * the spin button alone would send its previous value instead. */
  gtk_editable_set_text(GTK_EDITABLE(f->time[1]), "75");
  click(f->set_button);
  assert(s_rtc.log[0] == '\0');
  snprintf(text, sizeof(text), sc_i18n_string_get(SC_I18N_RTC_RANGE_FMT),
           "rtc_minute", 75, 0, 59);
  expect_text(m->footer_status, text);
  assert(strcmp(gtk_editable_get_text(GTK_EDITABLE(f->time[1])), "75") == 0);
  /* Should a year get past the calendar, the set refuses it unsent. */
  f->selecting = true;
  pick_date(f, 2100, 1, 1);
  f->selecting = false;
  click(f->set_button);
  assert(s_rtc.log[0] == '\0');
  snprintf(text, sizeof(text), sc_i18n_string_get(SC_I18N_RTC_RANGE_FMT),
           "rtc_year", 2100, 2000, 2099);
  expect_text(m->footer_status, text);
  puts("PASS: RTC calendar keeps to the device years");

  /* The clock runs on while the operator changes only the minute, typed
   * and not confirmed; all six values go out, then one COMMIT. */
  s_rtc.active[5] = 57;
  pick_date(f, 2026, 11, 17);
  gtk_editable_set_text(GTK_EDITABLE(f->time[1]), "5");
  click(f->set_button);
  assert(strcmp(s_rtc.log, "HELLO\n"
                           "SC_AUTH_BEGIN\n"
                           "SC_AUTH_PROVE\n"
                           "SC_SET_PARAM rtc_year 2026\n"
                           "SC_SET_PARAM rtc_month 11\n"
                           "SC_SET_PARAM rtc_day 17\n"
                           "SC_SET_PARAM rtc_hour 23\n"
                           "SC_SET_PARAM rtc_minute 5\n"
                           "SC_SET_PARAM rtc_second 50\n"
                           "SC_COMMIT_PARAMS\n"
                           "SC_GET_VALUES\n") == 0);
  const int set[6] = {2026, 11, 17, 23, 5, 50};
  assert(memcmp(s_rtc.active, set, sizeof(set)) == 0 && !s_rtc.dirty);
  expect_rtc_form(f, 2026, 11, 17, "23:05:50");
  expect_text(f->state_label, sc_i18n_string_get(SC_I18N_RTC_STATE_TRUSTED));
  expect_text(m->footer_status, sc_i18n_string_get(SC_I18N_RTC_SET_OK));
  puts("PASS: RTC set sends the whole date and time, then reads it back");

  /* A refused COMMIT keeps the entry and clears the staged values. */
  s_rtc.commit_reason = "utc_range";
  pick_date(f, 2000, 1, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(f->time[0]), 0.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(f->time[1]), 30.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(f->time[2]), 0.0);
  click(f->set_button);
  assert(strstr(s_rtc.log, "SC_SET_PARAM rtc_second 0\n"
                           "SC_COMMIT_PARAMS\n"
                           "SC_REVERT_PARAMS\n") != NULL &&
         strstr(s_rtc.log, SC_CMD_GET_VALUES) == NULL);
  assert(memcmp(s_rtc.active, set, sizeof(set)) == 0 && !s_rtc.dirty);
  expect_rtc_form(f, 2000, 1, 1, "00:30:00");
  snprintf(text, sizeof(text), sc_i18n_string_get(SC_I18N_RTC_SET_FAILED_FMT),
           "firmware: SC_COMMIT_FAILED reason=utc_range");
  expect_text(m->footer_status, text);
  s_rtc.commit_reason = NULL;

  /* A refused value stops before COMMIT. */
  s_rtc.reject_id = SC_RTC_HOUR_ID;
  click(f->set_button);
  assert(strstr(s_rtc.log, "SC_SET_PARAM rtc_hour 0\n"
                           "SC_REVERT_PARAMS\n") != NULL &&
         strstr(s_rtc.log, SC_CMD_COMMIT_PARAMS) == NULL && !s_rtc.dirty);
  assert(strstr(gtk_label_get_text(GTK_LABEL(m->footer_status)),
                "rtc_hour: firmware: SC_BAD_REQUEST out_of_range") != NULL);
  /* ... and says so when the REVERT fails too. */
  s_rtc.fail_revert = true;
  click(f->set_button);
  assert(s_rtc.dirty);
  assert(strstr(gtk_label_get_text(GTK_LABEL(m->footer_status)),
                "SC_REVERT_PARAMS transport failed: port closed") != NULL);
  s_rtc.fail_revert = false;
  s_rtc.reject_id = NULL;
  s_rtc.dirty = false;

  /* Without authentication nothing is staged. */
  s_rtc.reject_auth = true;
  click(f->set_button);
  assert(strcmp(s_rtc.log, "HELLO\nSC_AUTH_BEGIN\nSC_AUTH_PROVE\n") == 0);
  assert(strstr(gtk_label_get_text(GTK_LABEL(m->footer_status)),
                "AUTH_PROVE rejected") != NULL);
  s_rtc.reject_auth = false;
  puts("PASS: RTC set stops at the first error and clears the staging");

  /* A set the device took, but could not report back. */
  s_rtc.fail_values_after_commit = true;
  pick_date(f, 2028, 2, 29);
  click(f->set_button);
  assert(s_rtc.active[0] == 2028 && s_rtc.active[1] == 2 &&
         s_rtc.active[2] == 29);
  expect_text(f->state_label, sc_i18n_string_get(SC_I18N_RTC_STATE_UNKNOWN));
  assert(strstr(gtk_label_get_text(GTK_LABEL(m->footer_status)),
                "SC_GET_VALUES") != NULL);
  s_rtc.fail_values_after_commit = false;
  s_rtc.fail_values = false;

  /* Read from device: the running clock, without authentication. */
  const int later[6] = {2027, 2, 28, 7, 8, 9};
  memcpy(s_rtc.active, later, sizeof(later));
  s_rtc.integrity = 0;
  click(f->read_button);
  assert(strcmp(s_rtc.log, "SC_GET_VALUES\n") == 0);
  expect_rtc_form(f, 2027, 2, 28, "07:08:09");
  expect_text(f->state_label, sc_i18n_string_get(SC_I18N_RTC_STATE_UNTRUSTED));
  expect_text(m->footer_status, sc_i18n_string_get(SC_I18N_RTC_READ_OK));
  /* A failed read keeps the form; the state is no longer known. */
  s_rtc.fail_values = true;
  click(f->read_button);
  expect_rtc_form(f, 2027, 2, 28, "07:08:09");
  expect_text(f->state_label, sc_i18n_string_get(SC_I18N_RTC_STATE_UNKNOWN));
  assert(strstr(gtk_label_get_text(GTK_LABEL(m->footer_status)), "no reply") !=
         NULL);
  s_rtc.fail_values = false;
  puts("PASS: RTC read shows the running clock and its state");

  /* Detection and flashing own the port: neither button talks to it. */
  for (int busy = 0; busy < 2; ++busy) {
    state->detection_in_progress = busy == 0;
    state->flash_in_progress = busy == 1;
    GtkWidget *buttons[] = {f->set_button, f->read_button};
    for (unsigned i = 0; i < COUNTOF(buttons); ++i) {
      gtk_label_set_text(GTK_LABEL(m->footer_status), "");
      click(buttons[i]);
      assert(s_rtc.log[0] == '\0');
      expect_text(m->footer_status, sc_i18n_string_get(SC_I18N_RTC_BUSY));
    }
  }
  state->detection_in_progress = false;
  state->flash_in_progress = false;
  puts("PASS: RTC buttons wait for detection and flashing");

  /* Until a read succeeds, the form shows the computer's date and
   * 00:00:00 rather than the device's, so it offers no write. */
  s_rtc.fail_values = true;
  GtkWidget *unread = build_module_subtab(state, 3u);
  g_object_ref_sink(unread);
  ModuleSubtab *um = g_object_get_data(G_OBJECT(unread), "sc-values-subtab");
  assert(um->rtc != NULL && !gtk_widget_get_sensitive(um->rtc->set_button));
  expect_text(um->rtc->state_label,
              sc_i18n_string_get(SC_I18N_RTC_STATE_UNKNOWN));
  assert(strstr(gtk_label_get_text(GTK_LABEL(um->footer_status)), "no reply") !=
         NULL);
  s_rtc.fail_values = false;
  click(um->rtc->read_button);
  assert(gtk_widget_get_sensitive(um->rtc->set_button));
  expect_rtc_form(um->rtc, 2027, 2, 28, "07:08:09");
  g_object_unref(unread);
  puts("PASS: RTC set waits for the first good read");

  g_object_unref(page);
  state->core.modules[3].detected = false;
  state->connected = false;
  state->core.transport = saved_transport;
  if (saved_tz != NULL) {
    g_setenv("TZ", saved_tz, TRUE);
  } else {
    g_unsetenv("TZ");
  }
  tzset();
  g_free(saved_tz);
}

int main(void) {
  /* Render the mapped test window without a GPU or driver-specific state. */
  g_setenv("GSK_RENDERER", "cairo", TRUE);
  if (getenv("LC_ALL") != NULL && setlocale(LC_ALL, "") == NULL) {
    fprintf(stderr, "FAIL: requested locale is unavailable\n");
    return 1;
  }
  if (!gtk_init_check()) {
    fprintf(stderr, "FAIL: live-view tests require a display\n");
    return 1;
  }
  static AppState state;
  FakeClocks fake = {
      .oil = 125, .abs = 42, .egt0 = 805, .egt1 = -40, .valid = 15};
  static const ScTransportOps ops = {.send_sc_command = send_command};
  sc_core_init(&state.core);
  sc_transport_init_custom(&state.core.transport, &ops, &fake);
  state.core.modules[1].detected = true;
  snprintf(state.core.modules[1].port_path,
           sizeof(state.core.modules[1].port_path), "/dev/clocks");
  state.connected = true;
  GtkWidget *page = build_module_subtab(&state, 1u);
  g_object_ref_sink(page);
  ModuleSubtab *module = g_object_get_data(G_OBJECT(page), "sc-values-subtab");
  /* A real mapped page exercises the production visibility gate. */
  GtkWidget *window = gtk_window_new();
  gtk_window_set_child(GTK_WINDOW(window), page);
  gtk_window_present(GTK_WINDOW(window));
  assert(gtk_widget_get_mapped(page));
  assert(module->read_only && module->poll_id != 0u && module->rows->len == 4u);
  /* validity is metadata, never an extra reading row. */
  for (unsigned i = 0; i < module->rows->len; ++i) {
    ParamRow *item = g_ptr_array_index(module->rows, i);
    assert(strcmp(item->id, "valid") != 0);
  }
  char expected[40];
  snprintf(expected, sizeof(expected), "%.2f bar", 1.25);
  expect_reading(module, 0, expected);
  expect_reading(module, 1, "42 km/h");
  expect_reading(module, 2, "805 °C");
  expect_reading(module, 3, "-40 °C");
  /* Keep localized output; verify both decimal conventions when installed. */
  char *original_locale = g_strdup(setlocale(LC_NUMERIC, NULL));
  const char *locales[] = {"C", "en_US.UTF-8", "pl_PL.UTF-8"};
  const char *decimal[] = {"1.25 bar", "1.25 bar", "1,25 bar"};
  for (unsigned i = 0; i < COUNTOF(locales); ++i) {
    if (setlocale(LC_NUMERIC, locales[i]) != NULL) {
      refresh_readings(module);
      expect_reading(module, 0, decimal[i]);
      printf("PASS locale: %s\n", locales[i]);
    }
  }
  assert(setlocale(LC_NUMERIC, original_locale) != NULL);
  g_free(original_locale);
  fake.oil = 250;
  refresh_readings(module);
  snprintf(expected, sizeof(expected), "%.2f bar", 2.50);
  expect_reading(module, 0, expected);
  for (unsigned bit = 0; bit < 4u; ++bit) {
    fake.valid = 15u & ~(1u << bit);
    refresh_readings(module);
    /* Param order: oil, ABS, before-DPF EGT, inside-DPF EGT. */
    const unsigned row_index[] = {1, 0, 2, 3};
    const char *invalid[] = {"-- km/h", "-- bar", "-- °C", "-- °C"};
    expect_reading(module, row_index[bit], invalid[bit]);
  }
  fake.valid = 15u;
  state.detection_in_progress = true;
  unsigned calls = fake.calls;
  refresh_readings(module);
  assert(fake.calls == calls);
  expect_reading(module, 0, "-- bar");
  expect_reading(module, 1, "-- km/h");
  state.detection_in_progress = false;
  refresh_readings(module);
  expect_reading(module, 0, expected);
  state.flash_in_progress = true;
  calls = fake.calls;
  refresh_readings(module);
  assert(fake.calls == calls);
  expect_reading(module, 0, "-- bar");
  state.flash_in_progress = false;
  refresh_readings(module);
  expect_reading(module, 0, expected);
  gtk_widget_set_visible(page, FALSE);
  calls = fake.calls;
  refresh_readings(module);
  assert(fake.calls == calls);
  gtk_widget_set_visible(page, TRUE);
  fake.fail = true;
  refresh_readings(module);
  expect_reading(module, 0, "-- bar");
  fake.fail = false;
  refresh_readings(module);
  expect_reading(module, 0, expected);
  gtk_window_set_child(GTK_WINDOW(window), NULL);
  g_object_unref(page); /* destroys the poll timer and its state */
  gtk_window_destroy(GTK_WINDOW(window));

  GtkWidget *map = sc_map_tab_build(&state);
  g_object_ref_sink(map);
  window = gtk_window_new();
  gtk_window_set_child(GTK_WINDOW(window), map);
  gtk_window_present(GTK_WINDOW(window));
  assert(gtk_widget_get_mapped(map));
  ScMapTabState *m = sc_map_state(&state);
  sc_map_poll_tick(&state);
  assert(m->have_fix);
  state.flash_in_progress = true;
  calls = fake.calls;
  sc_map_poll_tick(&state);
  assert(!m->have_fix && fake.calls == calls);
  state.flash_in_progress = false;
  sc_map_poll_tick(&state);
  assert(m->have_fix);
  gtk_widget_set_visible(map, FALSE);
  calls = fake.calls;
  sc_map_poll_tick(&state);
  assert(fake.calls == calls);
  gtk_widget_set_visible(map, TRUE);
  fake.no_fix = true;
  sc_map_poll_tick(&state);
  assert(!m->have_fix);
  fake.no_fix = false;
  sc_map_poll_tick(&state);
  assert(m->have_fix);
  fake.fail = true;
  sc_map_poll_tick(&state);
  assert(!m->have_fix);
  fake.fail = false;
  state.core.modules[1].detected = false;
  sc_map_poll_tick(&state);
  assert(!m->have_fix);
  state.core.modules[1].detected = true;
  sc_map_poll_tick(&state);
  assert(m->have_fix);
  state.connected = false;
  sc_map_tab_set_connected(&state, false);
  assert(!m->have_fix);
  /* Destroying the page stops the poll timer and frees its state. */
  gtk_window_set_child(GTK_WINDOW(window), NULL);
  g_object_unref(map);
  assert(sc_map_state(&state) == 0 && state.map_tab_root == 0);
  sc_map_tab_dispose(&state); /* a second call does nothing */
  gtk_window_destroy(GTK_WINDOW(window));
  /* The whole Values tab: Clocks runs functional tests, so its page splits
   * into Settings and Tests; RTC_Clock has none and keeps the plain page. */
  state.connected = true;
  state.core.modules[3].detected = true;
  snprintf(state.core.modules[3].port_path,
           sizeof(state.core.modules[3].port_path), "/dev/clocks");
  GtkWidget *values = sc_values_tab_build(&state);
  GtkWidget *main_tabs = gtk_notebook_new();
  g_object_ref_sink(main_tabs);
  gtk_notebook_append_page(GTK_NOTEBOOK(main_tabs), gtk_label_new(""), NULL);
  gtk_notebook_append_page(GTK_NOTEBOOK(main_tabs), values, NULL);
  sc_values_tab_rebuild(&state, true);
  GtkWidget *modules = gtk_widget_get_first_child(values);
  assert(GTK_IS_NOTEBOOK(modules));
  assert(gtk_notebook_get_n_pages(GTK_NOTEBOOK(modules)) == 2);
  assert(strcmp(tab_text(modules, 0), SC_MODULE_CLOCKS) == 0);
  assert(strcmp(tab_text(modules, 1), SC_MODULE_CLOCK) == 0);
  GtkWidget *clocks = gtk_notebook_get_nth_page(GTK_NOTEBOOK(modules), 0);
  assert(GTK_IS_NOTEBOOK(clocks));
  assert(gtk_notebook_get_n_pages(GTK_NOTEBOOK(clocks)) == 2);
  assert(strcmp(tab_text(clocks, 0),
                sc_i18n_string_get(SC_I18N_VALUES_SUBTAB_SETTINGS)) == 0);
  assert(strcmp(tab_text(clocks, 1),
                sc_i18n_string_get(SC_I18N_VALUES_SUBTAB_TESTS)) == 0);
  assert(s_tests_tabs_built == 1u && s_tests_tab_index == 1u);
  assert(!GTK_IS_NOTEBOOK(gtk_notebook_get_nth_page(GTK_NOTEBOOK(modules), 1)));
  puts("PASS: Clocks page has the Settings and Tests sub-tabs");

  /* A double-click on a module row opens its sub-tab of the Values tab. */
  GtkWidget *list = gtk_list_box_new();
  g_object_ref_sink(list);
  for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
    state.module_rows[i] = gtk_list_box_row_new();
    gtk_list_box_append(GTK_LIST_BOX(list), state.module_rows[i]);
  }
  g_signal_connect(list, "row-activated",
                   G_CALLBACK(sc_values_tab_on_module_activated), &state);
  g_signal_emit_by_name(list, "row-activated", state.module_rows[3]);
  assert(gtk_notebook_get_current_page(GTK_NOTEBOOK(main_tabs)) == 1);
  assert(gtk_notebook_get_current_page(GTK_NOTEBOOK(modules)) == 1);
  g_signal_emit_by_name(list, "row-activated", state.module_rows[1]);
  assert(gtk_notebook_get_current_page(GTK_NOTEBOOK(modules)) == 0);
  /* ECU is not detected: it has no sub-tab and nothing moves. */
  gtk_notebook_set_current_page(GTK_NOTEBOOK(main_tabs), 0);
  g_signal_emit_by_name(list, "row-activated", state.module_rows[0]);
  assert(gtk_notebook_get_current_page(GTK_NOTEBOOK(main_tabs)) == 0);
  assert(gtk_notebook_get_current_page(GTK_NOTEBOOK(modules)) == 0);
  g_object_unref(list);
  memset(state.module_rows, 0, sizeof(state.module_rows));
  puts("PASS: a double-click on a module opens its Values sub-tab");

  g_object_unref(main_tabs);
  state.values_tab_root = NULL;
  state.core.modules[3].detected = false;
  state.connected = false;

  test_rtc_form(&state);
  test_window_close_ends_sessions(&state);

  state.core.modules[1].display_name = NULL;
  GtkWidget *unnamed = build_module_subtab(&state, 1u);
  g_object_ref_sink(unnamed);
  ModuleSubtab *unnamed_module =
      g_object_get_data(G_OBJECT(unnamed), "sc-values-subtab");
  assert(!unnamed_module->read_only);
  g_object_unref(unnamed);
  puts("PASS: read-only live values, units, polling, failures, GPS loss and "
       "reconnect");
  return 0;
}
