/* Production Values and Map polling against a fake Clocks transport. */
#include "../src/ui/sc_map_tab.c"
#include "../src/ui/sc_values_tab.c"
#undef NDEBUG
#include <assert.h>
#include <locale.h>

GtkWidget *sc_tests_tab_build(AppState *state, size_t index) {
  (void)state;
  (void)index;
  return gtk_label_new("");
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
  sc_map_tab_dispose(&state);
  gtk_window_set_child(GTK_WINDOW(window), NULL);
  g_object_unref(map);
  gtk_window_destroy(GTK_WINDOW(window));
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
