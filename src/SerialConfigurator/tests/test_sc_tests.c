/*
 * Host side of the ECU functional tests: catalog load, status decoding and
 * the authenticated actions, against a mock ECU that answers the SC_TEST_*
 * commands the way the firmware does (sc_command_handlers.c).
 */

#include "sc_core.h"
#include "sc_mock_ecu.h"
#include "sc_protocol.h"
#include "sc_tests.h"
#include "sc_transport.h"

#include <stdbool.h>
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

#define TEST_ASSERT_STR(got, want, msg)                                        \
  do {                                                                         \
    if (strcmp((got), (want)) != 0) {                                          \
      fprintf(stderr, "FAIL: %s - %s (line %d): got '%s', want '%s'\n",        \
              __func__, (msg), __LINE__, (got), (want));                       \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static ScCommandResult ok_result(const char *topic, const char *details) {
  ScCommandResult result;
  memset(&result, 0, sizeof(result));
  result.status = SC_COMMAND_STATUS_OK;
  (void)snprintf(result.status_token, sizeof(result.status_token), "%s",
                 SC_STATUS_OK);
  (void)snprintf(result.topic, sizeof(result.topic), "%s", topic);
  (void)snprintf(result.details, sizeof(result.details), "%s", details);
  (void)snprintf(result.response, sizeof(result.response), "SC_OK %s %s", topic,
                 details);
  return result;
}

/* ── Parsers ────────────────────────────────────────────────────────────── */

static int test_status_decodes_every_key_and_keeps_progress_generic(void) {
  const ScCommandResult result = ok_result(
      SC_REPLY_TAG_TEST_STATUS,
      "state=running runs=12 test=pot src=sc elapsed_ms=1234 seq=2/5 "
      "demand_x10=905 position_x10=-3 phase=hold rate=250 pass=1 passes=3 "
      "last=cyclic result=ok");
  ScTestStatus status;
  char error[256] = {0};
  TEST_ASSERT(sc_tests_parse_status(&result, &status, error, sizeof(error)),
              error);
  TEST_ASSERT(status.running, "running");
  TEST_ASSERT(status.runs == 12u, "runs");
  TEST_ASSERT_STR(status.test, "pot", "test");
  TEST_ASSERT_STR(status.source, SC_TEST_SOURCE_SC, "source");
  TEST_ASSERT(status.elapsed_ms == 1234u, "elapsed");
  TEST_ASSERT(status.seq_index == 2u && status.seq_count == 5u, "seq");
  TEST_ASSERT(status.drive_valid, "drive valid");
  TEST_ASSERT(status.demand_x10 == 905 && status.position_x10 == -3, "drive");
  TEST_ASSERT(status.field_count == 4u, "progress fields");
  TEST_ASSERT_STR(status.fields[0].key, SC_TEST_FIELD_PHASE, "phase key");
  TEST_ASSERT(status.fields[0].is_text, "phase is text");
  TEST_ASSERT_STR(status.fields[0].text, SC_TEST_PHASE_HOLD, "phase text");
  int32_t rate = 0;
  TEST_ASSERT(sc_tests_status_field(&status, SC_TEST_FIELD_RATE, &rate) &&
                  rate == 250,
              "rate field");
  TEST_ASSERT(!sc_tests_status_field(&status, SC_TEST_FIELD_PHASE, NULL),
              "text field has no number");
  TEST_ASSERT(status.has_last, "has last");
  TEST_ASSERT_STR(status.last, "cyclic", "last");
  TEST_ASSERT_STR(status.result, SC_TEST_RESULT_OK, "result");
  return 0;
}

static int test_status_idle_without_drive_and_history(void) {
  const ScCommandResult result =
      ok_result(SC_REPLY_TAG_TEST_STATUS, "state=idle runs=0");
  ScTestStatus status;
  char error[256] = {0};
  TEST_ASSERT(sc_tests_parse_status(&result, &status, error, sizeof(error)),
              error);
  TEST_ASSERT(!status.running && !status.drive_valid && !status.has_last,
              "idle status");
  TEST_ASSERT(status.field_count == 0u, "no progress");
  return 0;
}

static int test_status_rejects_malformed_replies(void) {
  static const char *const k_bad[] = {
      "runs=1",                       /* no state */
      "state=paused",                 /* unknown state */
      "state=running seq=6/5",        /* index past count */
      "state=running demand_x10=abc", /* not a number */
      "state=running elapsed_ms=-1",  /* negative counter */
      "state=running junk",           /* token without '=' */
  };
  for (size_t i = 0u; i < sizeof(k_bad) / sizeof(k_bad[0]); ++i) {
    const ScCommandResult result =
        ok_result(SC_REPLY_TAG_TEST_STATUS, k_bad[i]);
    ScTestStatus status;
    char error[256] = {0};
    TEST_ASSERT(!sc_tests_parse_status(&result, &status, error, sizeof(error)),
                k_bad[i]);
    TEST_ASSERT(error[0] != '\0', "error text");
  }
  const ScCommandResult other = ok_result(SC_REPLY_TAG_TEST_LIST, "count=0");
  ScTestStatus status;
  TEST_ASSERT(!sc_tests_parse_status(&other, &status, NULL, 0u), "wrong topic");
  return 0;
}

static int test_list_info_and_param_parsers_check_their_shape(void) {
  ScTestCatalog catalog;
  char error[256] = {0};
  ScCommandResult result =
      ok_result(SC_REPLY_TAG_TEST_LIST, "count=3 names=top,cyclic,pot");
  TEST_ASSERT(sc_tests_parse_list(&result, &catalog, error, sizeof(error)),
              error);
  TEST_ASSERT(catalog.supported && catalog.count == 3u, "list count");
  TEST_ASSERT_STR(catalog.tests[2].name, "pot", "third name");

  result = ok_result(SC_REPLY_TAG_TEST_LIST, "count=0 names=");
  TEST_ASSERT(sc_tests_parse_list(&result, &catalog, error, sizeof(error)),
              "empty production catalog");
  TEST_ASSERT(catalog.supported && catalog.count == 0u, "empty catalog");

  static const char *const k_bad_lists[] = {
      "count=2 names=top",
      "count=2 names=top,,cyclic",
      "count=1 names=top,",
      "names=top",
  };
  for (size_t i = 0u; i < sizeof(k_bad_lists) / sizeof(k_bad_lists[0]); ++i) {
    result = ok_result(SC_REPLY_TAG_TEST_LIST, k_bad_lists[i]);
    TEST_ASSERT(!sc_tests_parse_list(&result, &catalog, NULL, 0u),
                k_bad_lists[i]);
  }

  ScTestEntry entry;
  memset(&entry, 0, sizeof(entry));
  result = ok_result(SC_REPLY_TAG_TEST_INFO,
                     "name=pot seq=0 params=pot_rate,pot_hold");
  TEST_ASSERT(sc_tests_parse_info(&result, &entry, error, sizeof(error)),
              error);
  TEST_ASSERT(!entry.in_sequence && entry.param_count == 2u, "info shape");
  TEST_ASSERT_STR(entry.params[1].id, "pot_hold", "second param");
  result = ok_result(SC_REPLY_TAG_TEST_INFO, "name=pot seq=2 params=");
  TEST_ASSERT(!sc_tests_parse_info(&result, &entry, NULL, 0u), "bad seq flag");

  ScTestParam param;
  result = ok_result(SC_REPLY_TAG_TEST_PARAM,
                     "id=pot_rate test=pot value=-5 min=-10 max=2000 "
                     "default=250 unit=pct_per_s");
  TEST_ASSERT(sc_tests_parse_param(&result, &param, error, sizeof(error)),
              error);
  TEST_ASSERT(param.value == -5 && param.min == -10 && param.max == 2000 &&
                  param.default_value == 250,
              "param numbers");
  TEST_ASSERT_STR(param.unit, SC_TEST_UNIT_PCT_PER_S, "param unit");
  result = ok_result(SC_REPLY_TAG_TEST_PARAM,
                     "id=pot_rate test=pot value=5 min=10 max=20 unit=s");
  TEST_ASSERT(!sc_tests_parse_param(&result, &param, error, sizeof(error)),
              "missing default");
  result = ok_result(SC_REPLY_TAG_TEST_PARAM,
                     "id=a test=pot value=5 min=30 max=20 default=1 unit=s");
  TEST_ASSERT(!sc_tests_parse_param(&result, &param, NULL, 0u),
              "min above max");
  result = ok_result(SC_REPLY_TAG_TEST_PARAM,
                     "id=a test=pot value=99999999999 min=0 max=20 "
                     "default=1 unit=s");
  TEST_ASSERT(!sc_tests_parse_param(&result, &param, NULL, 0u),
              "value past int32");
  return 0;
}

/* ── Catalog and status through the core ────────────────────────────────── */

static int test_catalog_loads_tests_and_parameters(void) {
  sc_mock_ecu_reset();
  ScCore core;
  TEST_ASSERT(sc_mock_ecu_detected_core(&core), "detect");
  ScTestCatalog catalog;
  char error[256] = {0};
  TEST_ASSERT(sc_tests_load_catalog(&core, 0u, &catalog, error, sizeof(error)),
              error);
  TEST_ASSERT(catalog.supported && catalog.count == 2u, "two tests");
  const ScTestEntry *pot = &catalog.tests[0];
  TEST_ASSERT_STR(pot->name, "pot", "first test");
  TEST_ASSERT(!pot->in_sequence && pot->param_count == 0u, "pot");
  const ScTestEntry *cyclic = &catalog.tests[1];
  TEST_ASSERT(cyclic->in_sequence && cyclic->param_count == 2u, "cyclic");
  TEST_ASSERT_STR(cyclic->params[0].id, "cyclic_passes", "param id");
  TEST_ASSERT_STR(cyclic->params[0].test, "cyclic", "param owner");
  TEST_ASSERT(cyclic->params[0].value == 4 && cyclic->params[0].max == 100,
              "param range");
  TEST_ASSERT(cyclic->params[1].default_value == 6, "second param");
  return 0;
}

static int test_catalog_without_tests_is_empty_or_unsupported(void) {
  sc_mock_ecu_reset();
  s_ecu.production = true;
  ScCore core;
  TEST_ASSERT(sc_mock_ecu_detected_core(&core), "detect");
  ScTestCatalog catalog;
  char error[256] = {0};
  TEST_ASSERT(sc_tests_load_catalog(&core, 0u, &catalog, error, sizeof(error)),
              error);
  TEST_ASSERT(catalog.supported && catalog.count == 0u, "production ECU");

  s_ecu.tests_compiled = false;
  TEST_ASSERT(sc_tests_load_catalog(&core, 0u, &catalog, error, sizeof(error)),
              "older firmware still loads");
  TEST_ASSERT(!catalog.supported && catalog.count == 0u, "older firmware");

  ScTestStatus status;
  TEST_ASSERT(!sc_tests_get_status(&core, 0u, &status, error, sizeof(error)),
              "status of older firmware");
  return 0;
}

static int test_status_is_read_through_the_core(void) {
  sc_mock_ecu_reset();
  s_ecu.status_reply = "SC_OK TEST_STATUS state=running runs=3 test=cyclic "
                       "src=sc elapsed_ms=40 rate=250";
  ScCore core;
  TEST_ASSERT(sc_mock_ecu_detected_core(&core), "detect");
  ScTestStatus status;
  char error[256] = {0};
  TEST_ASSERT(sc_tests_get_status(&core, 0u, &status, error, sizeof(error)),
              error);
  TEST_ASSERT(status.running && status.runs == 3u, "running status");
  TEST_ASSERT(!status.drive_valid, "no drive reported");
  return 0;
}

/* ── Authenticated actions ──────────────────────────────────────────────── */

static int test_actions_map_every_firmware_answer(void) {
  sc_mock_ecu_reset();
  ScTransport transport;
  sc_transport_init_custom(&transport, &k_mock_ecu_ops, NULL);
  char error[256] = {0};

  TEST_ASSERT(sc_tests_run(&transport, MOCK_PATH, "cyclic", error,
                           sizeof(error)) == SC_TEST_ACTION_ERR_NOT_AUTHORIZED,
              "run without auth");
  TEST_ASSERT(strstr(error, SC_STATUS_NOT_AUTHORIZED) != NULL, "error text");

  s_ecu.authenticated = true;
  TEST_ASSERT(sc_tests_set_param(&transport, MOCK_PATH, "cyclic_passes", 7,
                                 error, sizeof(error)) == SC_TEST_ACTION_OK,
              "set");
  TEST_ASSERT(s_ecu.cyclic_passes == 7, "value reached the ECU");
  TEST_ASSERT(sc_tests_set_param(&transport, MOCK_PATH, "cyclic_passes", 500,
                                 error, sizeof(error)) ==
                  SC_TEST_ACTION_ERR_OUT_OF_RANGE,
              "out of range");
  TEST_ASSERT(sc_tests_set_param(&transport, MOCK_PATH, "missing", 1, error,
                                 sizeof(error)) ==
                  SC_TEST_ACTION_ERR_INVALID_PARAM,
              "unknown parameter");
  TEST_ASSERT(sc_tests_set_param(&transport, MOCK_PATH, "two words", 1, error,
                                 sizeof(error)) == SC_TEST_ACTION_ERR_NULL_ARG,
              "id that would split the command");

  TEST_ASSERT(sc_tests_run(&transport, MOCK_PATH, SC_TEST_SEQUENCE, error,
                           sizeof(error)) == SC_TEST_ACTION_OK,
              "run sequence");
  TEST_ASSERT_STR(s_ecu.last_run, SC_TEST_SEQUENCE, "sequence requested");
  TEST_ASSERT(sc_tests_run(&transport, MOCK_PATH, "dtc", error,
                           sizeof(error)) == SC_TEST_ACTION_ERR_UNKNOWN_TEST,
              "test the configurator is not offered");
  s_ecu.engine_running = true;
  TEST_ASSERT(sc_tests_run(&transport, MOCK_PATH, "cyclic", error,
                           sizeof(error)) == SC_TEST_ACTION_ERR_ENGINE_RUNNING,
              "interlock");
  s_ecu.engine_running = false;

  TEST_ASSERT(sc_tests_stop(&transport, MOCK_PATH, error, sizeof(error)) ==
                  SC_TEST_ACTION_OK,
              "stop");
  TEST_ASSERT(sc_tests_skip(&transport, MOCK_PATH, error, sizeof(error)) ==
                  SC_TEST_ACTION_OK,
              "skip");
  TEST_ASSERT(s_ecu.stops == 1u && s_ecu.skips == 1u, "stop and skip sent");
  TEST_ASSERT(sc_tests_stop(NULL, MOCK_PATH, error, sizeof(error)) ==
                  SC_TEST_ACTION_ERR_NULL_ARG,
              "no transport");
  return 0;
}

static int test_busy_is_retried_before_it_is_reported(void) {
  sc_mock_ecu_reset();
  s_ecu.authenticated = true;
  ScTransport transport;
  sc_transport_init_custom(&transport, &k_mock_ecu_ops, NULL);
  char error[256] = {0};

  s_ecu.busy_replies = 2u;
  const unsigned before = s_ecu.sent;
  TEST_ASSERT(sc_tests_run(&transport, MOCK_PATH, "cyclic", error,
                           sizeof(error)) == SC_TEST_ACTION_OK,
              "taken after two busy answers");
  TEST_ASSERT(s_ecu.sent - before == 3u, "three attempts");

  s_ecu.busy_replies = SC_TESTS_BUSY_RETRIES + 1u;
  TEST_ASSERT(sc_tests_stop(&transport, MOCK_PATH, error, sizeof(error)) ==
                  SC_TEST_ACTION_ERR_BUSY,
              "busy past the retries");
  TEST_ASSERT(s_ecu.stops == 0u, "stop never taken");
  return 0;
}

static int test_status_names_are_stable(void) {
  TEST_ASSERT_STR(sc_test_action_status_name(SC_TEST_ACTION_OK), "ok", "ok");
  TEST_ASSERT_STR(sc_test_action_status_name(SC_TEST_ACTION_ERR_ENGINE_RUNNING),
                  "engine_running", "engine running");
  return 0;
}

int main(void) {
  int failures = 0;
  failures += test_status_decodes_every_key_and_keeps_progress_generic();
  failures += test_status_idle_without_drive_and_history();
  failures += test_status_rejects_malformed_replies();
  failures += test_list_info_and_param_parsers_check_their_shape();
  failures += test_catalog_loads_tests_and_parameters();
  failures += test_catalog_without_tests_is_empty_or_unsupported();
  failures += test_status_is_read_through_the_core();
  failures += test_actions_map_every_firmware_answer();
  failures += test_busy_is_retried_before_it_is_reported();
  failures += test_status_names_are_stable();
  if (failures != 0) {
    fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
  }
  printf("sc_tests: all tests passed\n");
  return 0;
}
