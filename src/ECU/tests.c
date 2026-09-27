/**
 * @file tests.c
 * @brief The functional test suite: every test the ECU can run, with its
 * console name, what it drives, whether it belongs to the sequence and to
 * the configurator, and the fixture behind it.
 *
 * What each test does lives in tests_workers.c; the runner that starts,
 * sequences and supervises them lives in tests_helpers.c.
 */

#include "tests.h"

#if ECU_FUNCTIONAL_TESTS_ENABLED

#include "../common/scDefinitions/sc_protocol.h"
#include "tests_workers.h"

#include <hal/core/hal_array.h>

static const char *kvResult(void) {
  return testsWorkersKvLastResult()->ok ? SC_TEST_RESULT_OK
                                        : SC_TEST_RESULT_FAILED;
}

/* Sequence order is table order. dtc leaves a fault behind, kv writes flash
 * and manual never ends, so none of them is offered to the configurator. */
static const ecu_test_t s_tests[] = {
    {.id = START_TEST_DTC,
     .name = "dtc",
     .summary = "inject one diagnostic trouble code",
     .drivesDemand = false,
     .sequenced = true,
     .scSupported = false,
     .start = testsWorkersDtcStart,
     .step = NULL,
     .result = NULL,
     .progress = NULL},
    {.id = START_TEST_KV,
     .name = "kv",
     .summary = "write and read back one key-value counter",
     .drivesDemand = false,
     .sequenced = true,
     .scSupported = false,
     .start = testsWorkersKvStart,
     .step = NULL,
     .result = kvResult,
     .progress = NULL},
    {.id = START_TEST_CYCLIC,
     .name = SC_TEST_NAME_CYCLIC,
     .summary = "0-100-0 ramps over four step sizes",
     .drivesDemand = true,
     .sequenced = true,
     .scSupported = true,
     .start = testsWorkersCyclicStart,
     .step = testsWorkersCyclicStep,
     .result = NULL,
     .progress = testsWorkersCyclicProgress},
    {.id = START_TEST_RANDOM,
     .name = SC_TEST_NAME_RANDOM,
     .summary = "random positions, each held for a while",
     .drivesDemand = true,
     .sequenced = true,
     .scSupported = true,
     .start = testsWorkersRandomStart,
     .step = testsWorkersRandomStep,
     .result = NULL,
     .progress = testsWorkersRandomProgress},
    {.id = START_TEST_MANUAL,
     .name = "manual",
     .summary = "hold the demand of command S",
     .drivesDemand = true,
     .sequenced = false,
     .scSupported = false,
     .start = testsWorkersManualStart,
     .step = testsWorkersManualStep,
     .result = NULL,
     .progress = NULL},
    {.id = START_TEST_TOPSTEPS,
     .name = SC_TEST_NAME_TOP,
     .summary = "75-85.5-90.5-95-0 % steps, each held for the dwell",
     .drivesDemand = true,
     .sequenced = true,
     .scSupported = true,
     .start = testsWorkersTopStepsStart,
     .step = testsWorkersTopStepsStep,
     .result = NULL,
     .progress = testsWorkersTopStepsProgress},
    {.id = START_TEST_TOPZERO,
     .name = SC_TEST_NAME_TOPZERO,
     .summary = "the same thresholds, each from rest",
     .drivesDemand = true,
     .sequenced = true,
     .scSupported = true,
     .start = testsWorkersTopZeroStart,
     .step = testsWorkersTopZeroStep,
     .result = NULL,
     .progress = testsWorkersTopZeroProgress},
    {.id = START_TEST_POT,
     .name = SC_TEST_NAME_POT,
     .summary = "pot turn: tracked ramp to 100 %, hold, back",
     .drivesDemand = true,
     .sequenced = false,
     .scSupported = true,
     .start = testsWorkersPotStart,
     .step = testsWorkersPotStep,
     .result = NULL,
     .progress = testsWorkersPotProgress},
};

const ecu_test_t *testsSuite(size_t *outCount) {
  *outCount = COUNTOF(s_tests);
  return s_tests;
}

#endif /* ECU_FUNCTIONAL_TESTS_ENABLED */
