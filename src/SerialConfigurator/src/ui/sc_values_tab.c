#include "sc_values_tab.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc_clocks_measurements.h"
#include "sc_core.h"
#include "sc_i18n.h"
#include "sc_protocol.h"
#include "sc_rtc_clock_params.h"
#include "sc_tests_tab.h"
#include <hal/core/hal_array.h>

/* ── Per-row + per-subtab state, attached to widgets via
 *    g_object_set_data_full so destruction is automatic. ──────────── */

typedef struct ModuleSubtab ModuleSubtab;
typedef struct RtcForm RtcForm;

typedef struct ParamRow {
  char id[SC_PARAM_ID_MAX];
  int16_t applied_value; /* last value confirmed in firmware staging */
  GtkAdjustment *adjustment;
  GtkWidget *status_label;
  GtkWidget *read_label;
  bool dirty;
  bool suppress_change_signal;
} ParamRow;

struct ModuleSubtab {
  AppState *state;
  size_t module_index;
  GPtrArray *rows; /* of ParamRow* (owns) */
  GtkWidget *footer_status;
  bool is_rtc_clock;
  bool read_only;
  guint poll_id;
  GtkWidget *page;
  RtcForm *rtc; /* RTC_Clock date and time form (owns), NULL elsewhere */
};

static void param_row_free(gpointer data) { g_free(data); }

static void module_subtab_free(gpointer data) {
  ModuleSubtab *m = (ModuleSubtab *)data;
  if (m == NULL) {
    return;
  }
  if (m->poll_id != 0u) {
    g_source_remove(m->poll_id);
  }
  if (m->rows != NULL) {
    g_ptr_array_unref(m->rows);
  }
  g_free(m->rtc);
  g_free(m);
}

/* ── Helpers ─────────────────────────────────────────────────────── */

/* Render "cooling_fan" -> "Cooling Fan" for the section header.
 * Pure presentation - the wire token stays snake_case. */
static void format_group_label(const char *token, char *out, size_t out_size) {
  if (out == NULL || out_size == 0u) {
    return;
  }
  out[0] = '\0';
  if (token == NULL || token[0] == '\0') {
    snprintf(out, out_size, "-");
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

static int16_t typed_value_to_i16(const ScTypedValue *tv) {
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

static const ScModuleStatus *module_status_for(AppState *state, size_t idx) {
  if (state == NULL) {
    return NULL;
  }
  return sc_core_module_status(&state->core, idx);
}

static void footer_set_status(ModuleSubtab *m, const char *text) {
  if (m == NULL || m->footer_status == NULL) {
    return;
  }
  gtk_label_set_text(GTK_LABEL(m->footer_status), (text != NULL) ? text : "");
}

static void row_set_dirty_state(ParamRow *row, int16_t current_value) {
  if (row == NULL) {
    return;
  }
  row->dirty = (current_value != row->applied_value);
  if (row->status_label == NULL) {
    return;
  }

  if (row->dirty) {
    char buf[64];
    snprintf(buf, sizeof(buf), "edited (was %d)", (int)row->applied_value);
    gtk_label_set_text(GTK_LABEL(row->status_label), buf);
    return;
  }

  gtk_label_set_text(GTK_LABEL(row->status_label), "");
}

/* ── Row construction + value-changed handler ────────────────────── */

static void on_value_changed(GtkAdjustment *adj, gpointer user_data) {
  ParamRow *row = (ParamRow *)user_data;
  if (row == NULL || row->suppress_change_signal) {
    return;
  }
  const int16_t cur = (int16_t)gtk_adjustment_get_value(adj);
  row_set_dirty_state(row, cur);
}

static GtkWidget *build_param_row(ModuleSubtab *m,
                                  const ScParamDetailData *detail) {
  ParamRow *row = g_new0(ParamRow, 1);
  snprintf(row->id, sizeof(row->id), "%s", detail->id);
  row->applied_value = typed_value_to_i16(&detail->value);

  /* Spin/scale share one adjustment for tandem-sync without manual
   * binding code. The descriptor's [min, max] becomes the adjustment
   * range so the widget itself enforces clamping; the firmware
   * re-enforces the same bounds at SET_PARAM time. */
  const int16_t min_v =
      detail->has_min ? typed_value_to_i16(&detail->min) : INT16_MIN;
  const int16_t max_v =
      detail->has_max ? typed_value_to_i16(&detail->max) : INT16_MAX;
  const int16_t cur_v = row->applied_value;

  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top(hbox, 2);
  gtk_widget_set_margin_bottom(hbox, 2);

  GtkWidget *id_label = gtk_label_new(detail->id);
  gtk_label_set_xalign(GTK_LABEL(id_label), 0.0f);
  gtk_widget_set_size_request(id_label, 220, -1);
  gtk_box_append(GTK_BOX(hbox), id_label);

  if (m->read_only) {
    row->read_label = gtk_label_new(detail->value.raw);
    gtk_label_set_xalign(GTK_LABEL(row->read_label), 0.0f);
    gtk_box_append(GTK_BOX(hbox), row->read_label);
    g_ptr_array_add(m->rows, row);
    return hbox;
  }

  GtkAdjustment *adj = gtk_adjustment_new((double)cur_v, (double)min_v,
                                          (double)max_v, 1.0, 10.0, 0.0);
  row->adjustment = adj;

  GtkWidget *spin = gtk_spin_button_new(adj, 1.0, 0);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
  gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), GTK_UPDATE_IF_VALID);
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

  g_signal_connect(adj, "value-changed", G_CALLBACK(on_value_changed), row);

  g_ptr_array_add(m->rows, row);
  return hbox;
}

/* ── Footer actions: re-auth then SET / COMMIT / REVERT ──────────── */

/* Shows the i18n format @p key filled with @p err in the footer. */
static void footer_set_error(ModuleSubtab *m, ScI18nKey key, const char *err) {
  char buf[640];
  snprintf(buf, sizeof(buf), sc_i18n_string_get(key), err);
  footer_set_status(m, buf);
}

/* Re-authenticates the subtab's module before SET / COMMIT / REVERT.
 * Returns its status, or NULL after the footer shows why it failed. */
static const ScModuleStatus *authenticate_for_subtab(ModuleSubtab *m) {
  char err[512] = {0};
  const ScModuleStatus *st = module_status_for(m->state, m->module_index);
  if (st == NULL || !st->detected || st->port_path[0] == '\0') {
    snprintf(err, sizeof(err), "module not detected");
  } else if (sc_core_authenticate(&m->state->core.transport, st->port_path, err,
                                  sizeof(err)) == SC_AUTH_OK) {
    return st;
  }
  footer_set_error(m, SC_I18N_VALUES_AUTH_FAILED_FMT, err);
  return NULL;
}

static void on_apply_clicked(GtkButton *btn, gpointer user_data) {
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
    footer_set_status(m, sc_i18n_string_get(SC_I18N_VALUES_NOTHING_TO_APPLY));
    return;
  }

  const ScModuleStatus *st = authenticate_for_subtab(m);
  if (st == NULL) {
    return;
  }

  size_t applied = 0u;
  for (size_t i = 0u; i < m->rows->len; ++i) {
    ParamRow *row = (ParamRow *)g_ptr_array_index(m->rows, i);
    if (!row->dirty) {
      continue;
    }
    const int16_t v = (int16_t)gtk_adjustment_get_value(row->adjustment);
    char rerr[512] = {0};
    const ScSetParamStatus rc =
        sc_core_set_param(&m->state->core.transport, st->port_path, row->id, v,
                          rerr, sizeof(rerr));
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
             sc_i18n_string_get(SC_I18N_VALUES_APPLY_FAILED_FMT), row->id,
             rerr);
    footer_set_status(m, fbuf);
    return;
  }

  char ok_buf[200];
  snprintf(ok_buf, sizeof(ok_buf),
           sc_i18n_string_get(SC_I18N_VALUES_APPLY_OK_FMT), (unsigned)applied);
  footer_set_status(m, ok_buf);
}

static void on_commit_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  ModuleSubtab *m = (ModuleSubtab *)user_data;
  if (m == NULL) {
    return;
  }
  const ScModuleStatus *st = authenticate_for_subtab(m);
  if (st == NULL) {
    return;
  }
  char err[512] = {0};
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
  footer_set_error(m, SC_I18N_VALUES_COMMIT_FAILED_FMT, err);
}

static void on_revert_clicked(GtkButton *btn, gpointer user_data) {
  (void)btn;
  ModuleSubtab *m = (ModuleSubtab *)user_data;
  if (m == NULL) {
    return;
  }
  const ScModuleStatus *st = authenticate_for_subtab(m);
  if (st == NULL) {
    return;
  }
  char err[512] = {0};
  const ScRevertParamsStatus rc = sc_core_revert_params(
      &m->state->core.transport, st->port_path, err, sizeof(err));
  if (rc == SC_REVERT_PARAMS_OK) {
    /* Snap every spinner back to its applied value and clear
     * dirty markers; the firmware-side staging mirror is now
     * back to the active mirror. */
    for (size_t i = 0u; i < m->rows->len; ++i) {
      ParamRow *row = (ParamRow *)g_ptr_array_index(m->rows, i);
      row->suppress_change_signal = true;
      gtk_adjustment_set_value(row->adjustment, (double)row->applied_value);
      row->suppress_change_signal = false;
      row->dirty = false;
      if (row->status_label != NULL) {
        gtk_label_set_text(GTK_LABEL(row->status_label), "");
      }
    }
    footer_set_status(m, sc_i18n_string_get(SC_I18N_VALUES_REVERT_OK));
    return;
  }
  footer_set_error(m, SC_I18N_VALUES_REVERT_FAILED_FMT, err);
}

static const ScTypedValue *find_value(const ScParamValuesData *values,
                                      const char *id) {
  for (size_t i = 0u; i < values->count; ++i) {
    if (strcmp(values->entries[i].id, id) == 0) {
      return &values->entries[i].value;
    }
  }
  return NULL;
}

static void show_reading(ParamRow *row, const ScParamValuesData *values) {
  const ScTypedValue *value =
      values != NULL ? find_value(values, row->id) : NULL;
  unsigned valid_bit = 0u;
  const char *unit = "";
  double divisor = 1.0;
  static const struct {
    const char *id;
    const char *unit;
    unsigned bit;
    unsigned divisor;
  } measurements[] = {
#define SC_READING_ROW(id, bit, unit, divisor) {id, unit, bit, divisor},
      SC_CLOCKS_MEASUREMENTS(SC_READING_ROW)
#undef SC_READING_ROW
  };
  for (size_t i = 0u; i < COUNTOF(measurements); ++i) {
    if (strcmp(row->id, measurements[i].id) == 0) {
      valid_bit = measurements[i].bit;
      unit = measurements[i].unit;
      divisor = measurements[i].divisor;
      break;
    }
  }
  const ScTypedValue *valid = valid_bit != 0u && values != NULL
                                  ? find_value(values, SC_CLOCKS_VALID_ID)
                                  : NULL;
  char text[160];
  if (value == NULL || (valid_bit != 0u &&
                        (valid == NULL || ((unsigned)typed_value_to_i16(valid) &
                                           valid_bit) == 0u))) {
    snprintf(text, sizeof(text), "-- %s", unit);
  } else if (valid_bit != 0u) {
    snprintf(text, sizeof(text), divisor == 100.0 ? "%.2f %s" : "%.0f %s",
             (double)typed_value_to_i16(value) / divisor, unit);
  } else {
    snprintf(text, sizeof(text), "%s", value->raw);
  }
  gtk_label_set_text(GTK_LABEL(row->read_label), text);
}

/* Detection and flashing own the serial ports until they finish. */
static bool transport_busy(const AppState *state) {
  return state->detection_in_progress || state->flash_in_progress;
}

/* One SC_GET_VALUES snapshot of the module, or NULL with the reason in
 * @p error. Shared static scratch: every caller runs on the GTK main
 * thread. */
static const ScParamValuesData *read_values(ModuleSubtab *m, char *error,
                                            size_t error_size) {
  static ScCommandResult result;
  static ScParamValuesData values;
  char log[256] = {0};
  if (!sc_core_sc_get_values(&m->state->core, m->module_index, &result, log,
                             sizeof(log))) {
    snprintf(error, error_size, "%s", g_strstrip(log));
    return NULL;
  }
  if (!sc_core_parse_param_values_result(&result, &values, error, error_size)) {
    return NULL;
  }
  if (values.truncated) {
    snprintf(error, error_size, "%s", "truncated " SC_CMD_GET_VALUES " reply");
    return NULL;
  }
  return &values;
}

static gboolean refresh_readings(gpointer user) {
  ModuleSubtab *m = user;
  const bool busy = transport_busy(m->state);
  if (!busy && !gtk_widget_get_mapped(m->page)) {
    return G_SOURCE_CONTINUE;
  }
  char error[256] = {0};
  const ScModuleStatus *status = module_status_for(m->state, m->module_index);
  const ScParamValuesData *values =
      (!busy && m->state->connected && status != NULL && status->detected &&
       !status->target_ambiguous)
          ? read_values(m, error, sizeof(error))
          : NULL;
  for (size_t i = 0u; i < m->rows->len; ++i) {
    show_reading(g_ptr_array_index(m->rows, i), values);
  }
  return G_SOURCE_CONTINUE;
}

static void on_readings_mapped(GtkWidget *widget, gpointer user) {
  (void)widget;
  (void)refresh_readings(user);
}

/* ── RTC_Clock: one date and time form ───────────────────────────── */

/* The six values travel together: one SC_GET_VALUES snapshot fills the
 * form, and a write stages all six before a single COMMIT, so a running
 * clock never mixes an older reading with the device's newer state. The
 * date comes from the calendar and the time from the spin buttons exactly
 * as entered, in CET/CEST; the firmware converts it to UTC, so the host
 * time zone plays no part. */
enum {
  RTC_YEAR = 0,
  RTC_MONTH,
  RTC_DAY,
  RTC_HOUR,
  RTC_MINUTE,
  RTC_SECOND,
  RTC_FIELD_COUNT,
  RTC_TIME_FIELDS = RTC_FIELD_COUNT - RTC_HOUR
};

static const char *const k_rtc_ids[] = {
#define SC_RTC_ID_ROW(id) id,
    SC_RTC_DATETIME_PARAMS(SC_RTC_ID_ROW)
#undef SC_RTC_ID_ROW
};
_Static_assert(COUNTOF(k_rtc_ids) == RTC_FIELD_COUNT,
               "one form field per staged RTC parameter");

/* Bounds of the form itself (GDateTime takes years 1-9999); the device
 * metadata narrows them, e.g. to its supported years. */
static const int k_rtc_lower[RTC_FIELD_COUNT] = {1, 1, 1, 0, 0, 0};
static const int k_rtc_upper[RTC_FIELD_COUNT] = {9999, 12, 31, 23, 59, 59};
static const ScI18nKey k_rtc_time_names[RTC_TIME_FIELDS] = {
    SC_I18N_RTC_HOUR, SC_I18N_RTC_MINUTE, SC_I18N_RTC_SECOND};

struct RtcForm {
  int lower[RTC_FIELD_COUNT];
  int upper[RTC_FIELD_COUNT];
  bool selecting; /* the form itself is moving the calendar */
  GtkWidget *calendar;
  GtkWidget *time[RTC_TIME_FIELDS];
  GtkWidget *state_label;
  GtkWidget *set_button;
  GtkWidget *read_button;
};

/* Integer value of @p tv; false when it is absent or not an integer. */
static bool typed_value_int(const ScTypedValue *tv, int *out) {
  if (tv == NULL ||
      (tv->type != SC_VALUE_TYPE_INT && tv->type != SC_VALUE_TYPE_UINT)) {
    return false;
  }
  *out = typed_value_to_i16(tv);
  return true;
}

/* gtk_calendar_select_day() is deprecated since GTK 4.20; both select the
 * date the same way. */
static void calendar_set_date(GtkCalendar *calendar, GDateTime *date) {
#if GTK_CHECK_VERSION(4, 20, 0)
  gtk_calendar_set_date(calendar, date);
#else
  gtk_calendar_select_day(calendar, date);
#endif
}

/* Selects a date between 1 January of the first device year and
 * 31 December of the last one, with the day kept to the month length.
 * Only the date fields count, and a UTC GDateTime keeps them free of the
 * host time zone. */
static void rtc_select_date(RtcForm *f, int year, int month, int day) {
  if (year < f->lower[RTC_YEAR]) {
    year = f->lower[RTC_YEAR];
    month = 1;
    day = 1;
  } else if (year > f->upper[RTC_YEAR]) {
    year = f->upper[RTC_YEAR];
    month = 12;
    day = 31;
  }
  month = CLAMP(month, 1, 12);
  day =
      CLAMP(day, 1,
            (int)g_date_get_days_in_month((GDateMonth)month, (GDateYear)year));
  GDateTime *date = g_date_time_new_utc(year, month, day, 0, 0, 0.0);
  f->selecting = true;
  calendar_set_date(GTK_CALENDAR(f->calendar), date);
  f->selecting = false;
  g_date_time_unref(date);
}

static void rtc_calendar_date(const RtcForm *f, int *year, int *month,
                              int *day) {
  GDateTime *date = gtk_calendar_get_date(GTK_CALENDAR(f->calendar));
  g_date_time_get_ymd(date, year, month, day);
  g_date_time_unref(date);
}

/* Day, month and year navigation may step past the device years; the
 * date then moves to the nearest day within them. */
static void on_rtc_calendar_changed(GtkCalendar *calendar, gpointer user) {
  (void)calendar;
  RtcForm *f = user;
  if (f->selecting) {
    return;
  }
  int year = 0;
  int month = 0;
  int day = 0;
  rtc_calendar_date(f, &year, &month, &day);
  if (year < f->lower[RTC_YEAR] || year > f->upper[RTC_YEAR]) {
    rtc_select_date(f, year, month, day);
  }
}

/* Two digits, as on the clock: 7 shows as 07. */
static gboolean on_rtc_time_output(GtkSpinButton *spin, gpointer user) {
  (void)user;
  char text[16];
  snprintf(text, sizeof(text), "%02d", gtk_spin_button_get_value_as_int(spin));
  if (strcmp(gtk_editable_get_text(GTK_EDITABLE(spin)), text) != 0) {
    gtk_editable_set_text(GTK_EDITABLE(spin), text);
  }
  return TRUE;
}

static GtkWidget *rtc_time_spin(ScI18nKey name, int lower, int upper) {
  GtkAdjustment *adj = gtk_adjustment_new((double)lower, (double)lower,
                                          (double)upper, 1.0, 10.0, 0.0);
  GtkWidget *spin = gtk_spin_button_new(adj, 1.0, 0);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(spin),
                                 GTK_ORIENTATION_VERTICAL);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
  gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(spin), TRUE);
  gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(spin), GTK_UPDATE_IF_VALID);
  gtk_editable_set_width_chars(GTK_EDITABLE(spin), 2);
  gtk_widget_set_tooltip_text(spin, sc_i18n_string_get(name));
  gtk_accessible_update_property(GTK_ACCESSIBLE(spin),
                                 GTK_ACCESSIBLE_PROPERTY_LABEL,
                                 sc_i18n_string_get(name), -1);
  g_signal_connect(spin, "output", G_CALLBACK(on_rtc_time_output), NULL);
  on_rtc_time_output(GTK_SPIN_BUTTON(spin), NULL);
  return spin;
}

/* The form as six values. Typed spin text counts before it is confirmed,
 * and an entry outside the range stays outside it, for the caller to
 * refuse: the spin button would quietly keep its previous value. */
static void rtc_form_values(const RtcForm *f, int values[RTC_FIELD_COUNT]) {
  rtc_calendar_date(f, &values[RTC_YEAR], &values[RTC_MONTH], &values[RTC_DAY]);
  for (size_t i = 0u; i < RTC_TIME_FIELDS; ++i) {
    GtkSpinButton *spin = GTK_SPIN_BUTTON(f->time[i]);
    const char *text = gtk_editable_get_text(GTK_EDITABLE(spin));
    char *end = NULL;
    const long typed = strtol(text, &end, 10);
    if (end != text && *end == '\0' &&
        (typed < f->lower[RTC_HOUR + i] || typed > f->upper[RTC_HOUR + i])) {
      values[RTC_HOUR + i] = (int)CLAMP(typed, -99999L, 99999L);
      continue;
    }
    gtk_spin_button_update(spin);
    values[RTC_HOUR + i] = gtk_spin_button_get_value_as_int(spin);
  }
}

static void rtc_form_show(RtcForm *f, const int values[RTC_FIELD_COUNT]) {
  rtc_select_date(f, values[RTC_YEAR], values[RTC_MONTH], values[RTC_DAY]);
  for (size_t i = 0u; i < RTC_TIME_FIELDS; ++i) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(f->time[i]),
                              (double)values[RTC_HOUR + i]);
  }
}

/* rtc_integrity, read-only: 1 only while the RTC holds trusted UTC. */
static void rtc_show_state(const RtcForm *f, const ScTypedValue *integrity) {
  int trusted = 0;
  ScI18nKey key = SC_I18N_RTC_STATE_UNKNOWN;
  if (typed_value_int(integrity, &trusted)) {
    key = (trusted != 0) ? SC_I18N_RTC_STATE_TRUSTED
                         : SC_I18N_RTC_STATE_UNTRUSTED;
  }
  gtk_label_set_text(GTK_LABEL(f->state_label), sc_i18n_string_get(key));
}

/* Fills the form and the clock state from one SC_GET_VALUES snapshot; on
 * failure the form keeps what it shows and the state turns unknown. The
 * first good read enables writing: until then the calendar shows the
 * computer's date and the time 00:00:00, not the device's. */
static bool rtc_read_device(ModuleSubtab *m, char *error, size_t error_size) {
  RtcForm *f = m->rtc;
  const ScParamValuesData *values = read_values(m, error, error_size);
  int read[RTC_FIELD_COUNT] = {0};
  for (size_t i = 0u; values != NULL && i < RTC_FIELD_COUNT; ++i) {
    if (!typed_value_int(find_value(values, k_rtc_ids[i]), &read[i])) {
      snprintf(error, error_size, "%s missing", k_rtc_ids[i]);
      values = NULL;
    }
  }
  if (values == NULL) {
    rtc_show_state(f, NULL);
    return false;
  }
  rtc_form_show(f, read);
  rtc_show_state(f, find_value(values, SC_RTC_INTEGRITY_ID));
  gtk_widget_set_sensitive(f->set_button, TRUE);
  return true;
}

/* A failed write may leave values staged, which also holds off the GPS
 * time sync; REVERT returns staging to the running clock. */
static void rtc_abort_write(ModuleSubtab *m, const char *port_path,
                            const char *reason) {
  char revert_error[256] = {0};
  char text[1024];
  if (sc_core_revert_params(&m->state->core.transport, port_path, revert_error,
                            sizeof(revert_error)) == SC_REVERT_PARAMS_OK) {
    snprintf(text, sizeof(text), sc_i18n_string_get(SC_I18N_RTC_SET_FAILED_FMT),
             reason);
  } else {
    snprintf(text, sizeof(text),
             sc_i18n_string_get(SC_I18N_RTC_SET_REVERT_FAILED_FMT), reason,
             revert_error);
  }
  footer_set_status(m, text);
}

/* "Set date and time": all six values, then COMMIT, stopping at the first
 * error; then the clock as the device now reports it. */
static void on_rtc_set_clicked(GtkButton *button, gpointer user) {
  (void)button;
  ModuleSubtab *m = user;
  RtcForm *f = m->rtc;
  if (transport_busy(m->state)) {
    footer_set_status(m, sc_i18n_string_get(SC_I18N_RTC_BUSY));
    return;
  }
  int values[RTC_FIELD_COUNT];
  rtc_form_values(f, values);
  for (size_t i = 0u; i < RTC_FIELD_COUNT; ++i) {
    if (values[i] < f->lower[i] || values[i] > f->upper[i]) {
      char text[200];
      snprintf(text, sizeof(text), sc_i18n_string_get(SC_I18N_RTC_RANGE_FMT),
               k_rtc_ids[i], values[i], f->lower[i], f->upper[i]);
      footer_set_status(m, text);
      return;
    }
  }

  const ScModuleStatus *st = authenticate_for_subtab(m);
  if (st == NULL) {
    return;
  }
  char error[512] = {0};
  for (size_t i = 0u; i < RTC_FIELD_COUNT; ++i) {
    char reason[400] = {0};
    if (sc_core_set_param(&m->state->core.transport, st->port_path,
                          k_rtc_ids[i], (int16_t)values[i], reason,
                          sizeof(reason)) != SC_SET_PARAM_OK) {
      snprintf(error, sizeof(error), "%s: %s", k_rtc_ids[i], reason);
      rtc_abort_write(m, st->port_path, error);
      return;
    }
  }
  if (sc_core_commit_params(&m->state->core.transport, st->port_path, error,
                            sizeof(error)) != SC_COMMIT_PARAMS_OK) {
    rtc_abort_write(m, st->port_path, error);
    return;
  }

  char read_error[256] = {0};
  if (rtc_read_device(m, read_error, sizeof(read_error))) {
    footer_set_status(m, sc_i18n_string_get(SC_I18N_RTC_SET_OK));
  } else {
    footer_set_error(m, SC_I18N_RTC_SET_READ_FAILED_FMT, read_error);
  }
}

/* "Read from device": the clock as it runs now, no authentication. */
static void on_rtc_read_clicked(GtkButton *button, gpointer user) {
  (void)button;
  ModuleSubtab *m = user;
  if (transport_busy(m->state)) {
    footer_set_status(m, sc_i18n_string_get(SC_I18N_RTC_BUSY));
    return;
  }
  char error[256] = {0};
  if (rtc_read_device(m, error, sizeof(error))) {
    footer_set_status(m, sc_i18n_string_get(SC_I18N_RTC_READ_OK));
  } else {
    footer_set_error(m, SC_I18N_RTC_READ_FAILED_FMT, error);
  }
}

/* Narrows the bounds of one staged value to the device metadata. */
static bool rtc_load_bounds(ModuleSubtab *m, RtcForm *f, size_t field,
                            char *error, size_t error_size) {
  const char *id = k_rtc_ids[field];
  ScCommandResult result;
  char log[256] = {0};
  ScParamDetailData detail;
  char parse_error[256] = {0};
  if (!sc_core_sc_get_param(&m->state->core, m->module_index, id, &result, log,
                            sizeof(log))) {
    snprintf(error, error_size, "%s: %s", id, g_strstrip(log));
    return false;
  }
  if (!sc_core_parse_param_result(&result, &detail, parse_error,
                                  sizeof(parse_error))) {
    snprintf(error, error_size, "%s: %s", id, parse_error);
    return false;
  }
  int lower = k_rtc_lower[field];
  int upper = k_rtc_upper[field];
  int bound = 0;
  if (detail.has_min && typed_value_int(&detail.min, &bound) && bound > lower) {
    lower = bound;
  }
  if (detail.has_max && typed_value_int(&detail.max, &bound) && bound < upper) {
    upper = bound;
  }
  if (lower > upper) {
    snprintf(error, error_size, "%s: no usable range", id);
    return false;
  }
  f->lower[field] = lower;
  f->upper[field] = upper;
  return true;
}

static GtkWidget *rtc_heading(ScI18nKey key) {
  GtkWidget *label = gtk_label_new(NULL);
  char *markup = g_markup_printf_escaped("<b>%s</b>", sc_i18n_string_get(key));
  gtk_label_set_markup(GTK_LABEL(label), markup);
  g_free(markup);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  return label;
}

/* Clock state, calendar beside HH : MM : SS, then one footer with a shared
 * status for the whole date and time. */
static void build_rtc_form(ModuleSubtab *m, GtkWidget *form) {
  RtcForm *f = g_new0(RtcForm, 1);
  char error[300] = {0};
  for (size_t i = 0u; i < RTC_FIELD_COUNT; ++i) {
    if (!rtc_load_bounds(m, f, i, error, sizeof(error))) {
      g_free(f);
      char text[400];
      snprintf(text, sizeof(text),
               sc_i18n_string_get(SC_I18N_VALUES_LOAD_FAILED_FMT),
               SC_MODULE_CLOCK, error);
      GtkWidget *label = gtk_label_new(text);
      gtk_label_set_wrap(GTK_LABEL(label), TRUE);
      gtk_box_append(GTK_BOX(form), label);
      return;
    }
  }
  m->rtc = f;

  f->state_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(f->state_label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(f->state_label), TRUE);
  gtk_box_append(GTK_BOX(form), f->state_label);
  rtc_show_state(f, NULL);

  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 32);
  gtk_box_append(GTK_BOX(form), row);

  GtkWidget *date_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_append(GTK_BOX(date_box), rtc_heading(SC_I18N_RTC_DATE));
  f->calendar = gtk_calendar_new();
  gtk_widget_set_halign(f->calendar, GTK_ALIGN_START);
  static const char *const k_calendar_signals[] = {
      "day-selected", "next-month", "prev-month", "next-year", "prev-year"};
  for (size_t i = 0u; i < COUNTOF(k_calendar_signals); ++i) {
    g_signal_connect(f->calendar, k_calendar_signals[i],
                     G_CALLBACK(on_rtc_calendar_changed), f);
  }
  on_rtc_calendar_changed(GTK_CALENDAR(f->calendar), f);
  gtk_box_append(GTK_BOX(date_box), f->calendar);
  char years[64];
  snprintf(years, sizeof(years), sc_i18n_string_get(SC_I18N_RTC_YEARS_FMT),
           f->lower[RTC_YEAR], f->upper[RTC_YEAR]);
  GtkWidget *years_label = gtk_label_new(years);
  gtk_label_set_xalign(GTK_LABEL(years_label), 0.0f);
  gtk_widget_add_css_class(years_label, "dim-label");
  gtk_box_append(GTK_BOX(date_box), years_label);
  gtk_box_append(GTK_BOX(row), date_box);

  GtkWidget *time_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_append(GTK_BOX(time_box), rtc_heading(SC_I18N_RTC_LOCAL_TIME));
  GtkWidget *hms = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_halign(hms, GTK_ALIGN_START);
  for (size_t i = 0u; i < RTC_TIME_FIELDS; ++i) {
    if (i != 0u) {
      gtk_box_append(GTK_BOX(hms), gtk_label_new(":"));
    }
    f->time[i] = rtc_time_spin(k_rtc_time_names[i], f->lower[RTC_HOUR + i],
                               f->upper[RTC_HOUR + i]);
    gtk_box_append(GTK_BOX(hms), f->time[i]);
  }
  gtk_box_append(GTK_BOX(time_box), hms);
  gtk_box_append(GTK_BOX(row), time_box);

  GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top(footer, 8);
  f->set_button =
      gtk_button_new_with_label(sc_i18n_string_get(SC_I18N_RTC_BTN_SET));
  gtk_widget_add_css_class(f->set_button, "suggested-action");
  gtk_widget_set_sensitive(f->set_button, FALSE);
  f->read_button =
      gtk_button_new_with_label(sc_i18n_string_get(SC_I18N_RTC_BTN_READ));
  g_signal_connect(f->set_button, "clicked", G_CALLBACK(on_rtc_set_clicked), m);
  g_signal_connect(f->read_button, "clicked", G_CALLBACK(on_rtc_read_clicked),
                   m);
  gtk_box_append(GTK_BOX(footer), f->set_button);
  gtk_box_append(GTK_BOX(footer), f->read_button);
  m->footer_status = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(m->footer_status), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(m->footer_status), TRUE);
  gtk_widget_set_hexpand(m->footer_status, TRUE);
  gtk_box_append(GTK_BOX(footer), m->footer_status);
  gtk_box_append(GTK_BOX(m->page), footer);

  if (!transport_busy(m->state) && !rtc_read_device(m, error, sizeof(error))) {
    footer_set_error(m, SC_I18N_RTC_READ_FAILED_FMT, error);
  }
}

/* ── Build a per-module sub-tab ──────────────────────────────────── */

static GtkWidget *build_module_subtab(AppState *state, size_t module_index) {
  ModuleSubtab *m = g_new0(ModuleSubtab, 1);
  m->state = state;
  m->module_index = module_index;
  m->rows = g_ptr_array_new_with_free_func(param_row_free);

  GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  m->page = outer;
  gtk_widget_set_margin_start(outer, 12);
  gtk_widget_set_margin_end(outer, 12);
  gtk_widget_set_margin_top(outer, 12);
  gtk_widget_set_margin_bottom(outer, 12);

  /* Tying the lifetime of the ModuleSubtab struct (and its rows
   * GPtrArray) to the outer box lets us forget about manual cleanup
   * when the Values tab is rebuilt. */
  g_object_set_data_full(G_OBJECT(outer), "sc-values-subtab", m,
                         module_subtab_free);

  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_widget_set_hexpand(scroll, TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
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
  char cmd_log[256] = {0};
  const ScModuleStatus *target_status =
      sc_core_module_status(&state->core, module_index);
  const char *target_name =
      (target_status != NULL && target_status->display_name != NULL)
          ? target_status->display_name
          : "?";
  m->read_only = strcmp(target_name, SC_MODULE_CLOCKS) == 0;
  m->is_rtc_clock =
      (target_status != NULL && target_status->display_name != NULL &&
       strcmp(target_status->display_name, SC_MODULE_CLOCK) == 0);

  if (!sc_core_sc_get_param_list(&state->core, module_index, &result, cmd_log,
                                 sizeof(cmd_log))) {
    char buf[300];
    snprintf(buf, sizeof(buf),
             sc_i18n_string_get(SC_I18N_VALUES_LOAD_FAILED_FMT), target_name,
             "transport");
    gtk_box_append(GTK_BOX(form), gtk_label_new(buf));
    return outer;
  }

  ScParamListData list;
  char parse_err[256];
  if (!sc_core_parse_param_list_result(&result, &list, parse_err,
                                       sizeof(parse_err))) {
    char buf[300];
    snprintf(buf, sizeof(buf),
             sc_i18n_string_get(SC_I18N_VALUES_LOAD_FAILED_FMT), target_name,
             parse_err);
    gtk_box_append(GTK_BOX(form), gtk_label_new(buf));
    return outer;
  }

  if (m->is_rtc_clock) {
    build_rtc_form(m, form);
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
    char pl[256] = {0};
    if (!sc_core_sc_get_param(&state->core, module_index, list.ids[i], &pr, pl,
                              sizeof(pl))) {
      continue;
    }
    ScParamDetailData detail;
    char err2[256];
    if (!sc_core_parse_param_result(&pr, &detail, err2, sizeof(err2))) {
      continue;
    }
    if (m->read_only && strcmp(detail.group, "validity") == 0) {
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
      snprintf(markup, sizeof(markup), "<b><big>%s</big></b>", label_text);
      GtkWidget *header = gtk_label_new(NULL);
      gtk_label_set_markup(GTK_LABEL(header), markup);
      gtk_label_set_xalign(GTK_LABEL(header), 0.0f);
      gtk_box_append(GTK_BOX(section), header);

      GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
      gtk_box_append(GTK_BOX(section), separator);

      section_boxes[seen_count] = section;
      section_idx = seen_count;
      seen_count++;
    }

    GtkWidget *row_widget = build_param_row(m, &detail);
    gtk_box_append(GTK_BOX(section_boxes[section_idx]), row_widget);
  }

  if (m->read_only) {
    g_signal_connect(outer, "map", G_CALLBACK(on_readings_mapped), m);
    m->poll_id = g_timeout_add(1000u, refresh_readings, m);
    return outer;
  }

  /* Footer with [Apply staged] [Commit] [Revert] + status label. */
  GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top(footer, 8);

  GtkWidget *apply = gtk_button_new_with_label(
      sc_i18n_string_get(SC_I18N_VALUES_BTN_APPLY_STAGED));
  GtkWidget *commit =
      gtk_button_new_with_label(sc_i18n_string_get(SC_I18N_VALUES_BTN_COMMIT));
  GtkWidget *revert =
      gtk_button_new_with_label(sc_i18n_string_get(SC_I18N_VALUES_BTN_REVERT));

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

GtkWidget *sc_values_tab_build(AppState *state) {
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

  GtkWidget *placeholder =
      gtk_label_new(sc_i18n_string_get(SC_I18N_VALUES_PLACEHOLDER));
  gtk_widget_set_vexpand(placeholder, TRUE);
  gtk_box_append(GTK_BOX(outer), placeholder);

  state->values_tab_root = outer;
  return outer;
}

static void clear_box_children(GtkWidget *box) {
  GtkWidget *child = gtk_widget_get_first_child(box);
  while (child != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(box), child);
    child = next;
  }
}

void sc_values_tab_rebuild(AppState *state, bool detected_any) {
  if (state == NULL || state->values_tab_root == NULL) {
    return;
  }
  GtkWidget *outer = state->values_tab_root;
  clear_box_children(outer);

  if (!detected_any) {
    GtkWidget *placeholder =
        gtk_label_new(sc_i18n_string_get(SC_I18N_VALUES_PLACEHOLDER));
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
    GtkWidget *empty =
        gtk_label_new(sc_i18n_string_get(SC_I18N_VALUES_NO_DETECTED_MODULES));
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
    if (sc_core_module_has_tests(i)) {
      /* A module that runs functional tests (ECU, Clocks) gets a page
       * split into the parameter form and the tests the firmware reports. */
      GtkWidget *inner = gtk_notebook_new();
      gtk_notebook_append_page(
          GTK_NOTEBOOK(inner), page,
          gtk_label_new(sc_i18n_string_get(SC_I18N_VALUES_SUBTAB_SETTINGS)));
      gtk_notebook_append_page(
          GTK_NOTEBOOK(inner), sc_tests_tab_build(state, i),
          gtk_label_new(sc_i18n_string_get(SC_I18N_VALUES_SUBTAB_TESTS)));
      page = inner;
    }
    /* Index + 1, so a page without the tag reads as NULL. */
    g_object_set_data(G_OBJECT(page), "sc-module-index",
                      GSIZE_TO_POINTER(i + 1u));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page,
                             gtk_label_new(st->display_name));
  }
}

bool sc_values_tab_show_module(AppState *state, size_t module_index) {
  if (state == NULL || state->values_tab_root == NULL) {
    return false;
  }
  GtkWidget *modules = gtk_widget_get_first_child(state->values_tab_root);
  if (!GTK_IS_NOTEBOOK(modules)) {
    return false;
  }
  const int pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(modules));
  for (int i = 0; i < pages; ++i) {
    GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(modules), i);
    if (GPOINTER_TO_SIZE(g_object_get_data(
            G_OBJECT(page), "sc-module-index")) != module_index + 1u) {
      continue;
    }
    gtk_notebook_set_current_page(GTK_NOTEBOOK(modules), i);
    GtkWidget *main =
        gtk_widget_get_ancestor(state->values_tab_root, GTK_TYPE_NOTEBOOK);
    if (main != NULL) {
      gtk_notebook_set_current_page(
          GTK_NOTEBOOK(main),
          gtk_notebook_page_num(GTK_NOTEBOOK(main), state->values_tab_root));
    }
    return true;
  }
  return false;
}

void sc_values_tab_on_module_activated(GtkListBox *box, GtkListBoxRow *row,
                                       gpointer user_data) {
  (void)box;
  AppState *state = (AppState *)user_data;
  if (state == NULL || row == NULL) {
    return;
  }
  for (size_t i = 0u; i < SC_MODULE_COUNT; ++i) {
    if (state->module_rows[i] == GTK_WIDGET(row)) {
      (void)sc_values_tab_show_module(state, i);
      return;
    }
  }
}
