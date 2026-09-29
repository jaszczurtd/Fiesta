#include "sc_core.h"
#include "sc_transport.h"

#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(cond, msg)                                                 \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "[FAIL] %s\n", msg);                                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static int test_null_safety(void) {
  sc_core_init(0);
  sc_core_set_transport(0, 0);
  sc_core_reset_detection(0);
  sc_core_detect_modules(0, 0, 0u);

  char log[128] = {0};
  sc_core_detect_modules(0, log, sizeof(log));
  TEST_ASSERT(strstr(log, "Core is not initialized") != 0,
              "missing NULL-core error log");

  ScCommandResult result;
  TEST_ASSERT(!sc_core_sc_get_meta(0, 0u, &result, 0, 0u),
              "SC_GET_META should fail for NULL core");
  TEST_ASSERT(!sc_core_sc_get_param_list(0, 0u, &result, 0, 0u),
              "SC_GET_PARAM_LIST should fail for NULL core");
  TEST_ASSERT(!sc_core_sc_get_values(0, 0u, &result, 0, 0u),
              "SC_GET_VALUES should fail for NULL core");
  TEST_ASSERT(!sc_core_sc_bye(0, 0u, &result, 0, 0u),
              "SC_BYE should fail for NULL core");
  TEST_ASSERT(!sc_core_sc_get_param(0, 0u, "id", &result, 0, 0u),
              "SC_GET_PARAM should fail for NULL core");
  return 0;
}

static int test_status_contract(void) {
  ScCore core;
  sc_core_init(&core);

  TEST_ASSERT(sc_core_module_status(&core, SC_MODULE_COUNT) == 0,
              "status should be NULL for out-of-range index");
  TEST_ASSERT(sc_core_module_status(&core, SC_MODULE_COUNT + 10u) == 0,
              "status should be NULL for far out-of-range index");

  for (size_t i = 0u; i < sc_core_module_count(); ++i) {
    const ScModuleStatus *status = sc_core_module_status(&core, i);
    TEST_ASSERT(status != 0, "status should not be NULL");
    TEST_ASSERT(status->display_name != 0, "display_name should not be NULL");
    TEST_ASSERT(status->display_name[0] != '\0',
                "display_name should not be empty");
  }

  return 0;
}

static int test_module_table(void) {
  TEST_ASSERT(sc_core_module_source_dir(SC_MODULE_COUNT) == 0,
              "source dir should be NULL for out-of-range index");
  TEST_ASSERT(!sc_core_module_has_tests(SC_MODULE_COUNT),
              "out-of-range index has no tests");

  ScCore core;
  sc_core_init(&core);
  size_t with_tests = 0u;
  for (size_t i = 0u; i < sc_core_module_count(); ++i) {
    const char *name = sc_core_module_status(&core, i)->display_name;
    const char *dir = sc_core_module_source_dir(i);
    TEST_ASSERT(dir != 0 && dir[0] != '\0', "source dir should not be empty");
    if (strcmp(name, SC_MODULE_ECU) == 0) {
      TEST_ASSERT(strcmp(dir, "ECU") == 0, "ECU source dir");
      TEST_ASSERT(sc_core_module_has_tests(i), "ECU reports tests");
    }
    if (strcmp(name, SC_MODULE_CLOCK) == 0) {
      TEST_ASSERT(strcmp(dir, "Fiesta_clock") == 0,
                  "RTC_Clock builds from src/Fiesta_clock");
    }
    if (sc_core_module_has_tests(i)) {
      ++with_tests;
    }
  }
  TEST_ASSERT(with_tests == 1u, "only the ECU has a Tests tab");
  return 0;
}

static int test_out_of_scope_candidates(void) {
  TEST_ASSERT(sc_transport_candidate_out_of_scope(
                  "/dev/serial/by-id/usb-Jaszczur_Fiesta_Adjustometer_E6-if00"),
              "Adjustometer is never probed");
  TEST_ASSERT(!sc_transport_candidate_out_of_scope(
                  "/dev/serial/by-id/usb-Jaszczur_Fiesta_ECU_E6-if00"),
              "ECU is probed");
  TEST_ASSERT(!sc_transport_candidate_out_of_scope(
                  "/dev/serial/by-id/usb-Jaszczur_Fiesta_RTC_Clock_E6-if00"),
              "RTC clock is probed");
  TEST_ASSERT(!sc_transport_candidate_out_of_scope(0), "NULL path is kept");
  return 0;
}

static bool mock_list_candidates(void *context, ScTransportCandidateList *list,
                                 char *error, size_t error_size) {
  (void)error;
  (void)error_size;
  unsigned *calls = context;
  ++*calls;
  memset(list, 0, sizeof(*list));
  return true;
}

static int test_detect_modules_preserves_status_layout(void) {
  ScCore core;
  sc_core_init(&core);
  // Host API checks must never enumerate or send HELLO to attached hardware.
  unsigned list_calls = 0u;
  static const ScTransportOps ops = {.list_candidates = mock_list_candidates};
  ScTransport transport;
  sc_transport_init_custom(&transport, &ops, &list_calls);
  sc_core_set_transport(&core, &transport);

  char log[2048];
  sc_core_detect_modules(&core, log, sizeof(log));
  TEST_ASSERT(list_calls == 1u, "detection must use the injected transport");

  for (size_t i = 0u; i < sc_core_module_count(); ++i) {
    const ScModuleStatus *status = sc_core_module_status(&core, i);
    TEST_ASSERT(status != 0, "status should not be NULL after detect");
    TEST_ASSERT(status->display_name != 0,
                "display_name should not be NULL after detect");
    TEST_ASSERT(status->display_name[0] != '\0',
                "display_name should not be empty after detect");
  }

  return 0;
}

int main(void) {
  if (test_null_safety() != 0) {
    return 1;
  }

  if (test_status_contract() != 0) {
    return 1;
  }

  if (test_detect_modules_preserves_status_layout() != 0) {
    return 1;
  }

  if (test_module_table() != 0 || test_out_of_scope_candidates() != 0) {
    return 1;
  }

  printf("[OK] serial_configurator_core API behavior tests passed\n");
  return 0;
}
