#include "tests.h"

#if ECU_FUNCTIONAL_TESTS_ENABLED

#include "ecuContext.h"
#include "test_helpers.h"
#include "vp37.h"

//=============================================================================
// Registry
//=============================================================================

/** @brief One entry of the test registry. */
typedef struct {
  ecu_test_id_t id;
  const char *name;    /**< Console name after "run". */
  const char *summary; /**< One line for "list". */
  bool drivesDemand;   /**< Owns the actuator demand while running. */
  bool sequenced;      /**< Part of the "run all" sequence. */
  void (*start)(void); /**< Arms the test; NULL for a pure one-shot. */
  float (*step)(bool *outFinished); /**< Demand per step; NULL finishes at
                                       once, which is how a one-shot ends. */
} ecu_test_t;

static const ecu_test_t s_tests[] = {
    {START_TEST_DTC, "dtc", "inject one diagnostic trouble code", false, true,
     testHelpersDtcStart, NULL},
    {START_TEST_KV, "kv", "write and read back one key-value counter", false,
     true, testHelpersKvStart, NULL},
    {START_TEST_CYCLIC, "cyclic", "0-100-0 ramps over four step sizes", true,
     true, testHelpersCyclicStart, testHelpersCyclicStep},
    {START_TEST_RANDOM, "random", "random positions, each held for a while",
     true, true, testHelpersRandomStart, testHelpersRandomStep},
    {START_TEST_MANUAL, "manual", "hold the demand of command S", true, false,
     testHelpersManualStart, testHelpersManualStep},
    {START_TEST_TOPSTEPS, "top", "75-85.5-90.5-95-0 % steps, two seconds each",
     true, true, testHelpersTopStepsStart, testHelpersTopStepsStep},
    {START_TEST_TOPZERO, "topzero", "the same thresholds, each from rest", true,
     true, testHelpersTopZeroStart, testHelpersTopZeroStep},
};

static bool s_initialized = false;
static ecu_test_id_t s_active = START_TEST_NONE;
static bool s_sequenceRunning = false;
static size_t s_sequenceIndex = 0U;

static const ecu_test_t *findById(ecu_test_id_t id) {
  for (size_t i = 0U; i < COUNTOF(s_tests); i++) {
    if (s_tests[i].id == id) {
      return &s_tests[i];
    }
  }
  return NULL;
}

static const ecu_test_t *findByName(const char *name) {
  for (size_t i = 0U; i < COUNTOF(s_tests); i++) {
    if (testHelpersEquals(name, s_tests[i].name)) {
      return &s_tests[i];
    }
  }
  return NULL;
}

//=============================================================================
// Activation
//=============================================================================

static void activate(const ecu_test_t *entry) {
  s_active = entry->id;
  // Announce before arming, so whatever the fixture prints reads as its own.
  deb("TEST: %s started", entry->name);
  if (entry->start != NULL) {
    entry->start();
  }
}

/** @brief Release the running test and command zero if it drove the actuator.
 */
static void deactivate(void) {
  const ecu_test_t *entry = findById(s_active);
  if (entry == NULL) {
    s_active = START_TEST_NONE;
    return;
  }
  if (entry->drivesDemand) {
    (void)VP37_setPositionDemandPercentage(&getECUContext()->injectionPump,
                                           0.0f);
  }
  s_active = START_TEST_NONE;
  deb("TEST: %s stopped", entry->name);
}

/** @brief Start the next sequenced test; ends the sequence when none is left.
 */
static void advanceSequence(void) {
  while (s_sequenceIndex < COUNTOF(s_tests)) {
    const ecu_test_t *entry = &s_tests[s_sequenceIndex];
    s_sequenceIndex++;
    if (entry->sequenced) {
      activate(entry);
      return;
    }
  }
  s_sequenceRunning = false;
  deb("TEST: sequence finished");
}

static void beginSequence(void) {
  deactivate();
  s_sequenceIndex = 0U;
  s_sequenceRunning = true;
  deb("TEST: sequence started");
  advanceSequence();
}

//=============================================================================
// Public interface
//=============================================================================

bool initTests(void) {
  s_active = START_TEST_NONE;
  s_sequenceRunning = false;
  s_sequenceIndex = 0U;
  s_initialized = testHelpersReset();
  return s_initialized;
}

bool startTests(void) {
  return startTest(ECU_FUNCTIONAL_TESTS_AUTOSTART) == HAL_OK;
}

hal_status_t startTest(ecu_test_id_t test) {
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  if (test == START_TEST_NONE) {
    return stopTests();
  }
  if (test == START_TEST_ALL) {
    beginSequence();
    return HAL_OK;
  }
  const ecu_test_t *entry = findById(test);
  if (entry == NULL) {
    return HAL_EINVAL;
  }
  if (s_active != entry->id) {
    deactivate();
  }
  s_sequenceRunning = false;
  activate(entry);
  return HAL_OK;
}

hal_status_t stopTest(void) {
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  const bool wasActive = s_active != START_TEST_NONE;
  deactivate();
  if (s_sequenceRunning) {
    advanceSequence();
  } else if (!wasActive) {
    deb("TEST: none running");
  } else {
    // The stopped test already reported itself.
  }
  return HAL_OK;
}

hal_status_t stopTests(void) {
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  const bool wasActive = s_active != START_TEST_NONE;
  deactivate();
  s_sequenceRunning = false;
  if (!wasActive) {
    deb("TEST: none running");
  }
  return HAL_OK;
}

const char *testsActiveName(void) {
  const ecu_test_t *entry = findById(s_active);
  return entry != NULL ? entry->name : NULL;
}

uint32_t testsCyclicDelayMs(void) {
  return s_active == START_TEST_CYCLIC ? testHelpersCyclicDelayMs() : 0U;
}

//=============================================================================
// Console
//=============================================================================

static void printHelp(void) {
  deb(TEST_HIGHLIGHT_ON "Tests: run <name> | run all | stop | skip | list | "
                        "?" TEST_HIGHLIGHT_OFF);
  testHelpersPrintParameters();
}

static void printList(void) {
  const char *active = testsActiveName();
  for (size_t i = 0U; i < COUNTOF(s_tests); i++) {
    deb("TEST: %-7s %-42s %s%s", s_tests[i].name, s_tests[i].summary,
        s_tests[i].sequenced ? "in sequence" : "on request",
        testHelpersEquals(active, s_tests[i].name) ? ", running" : "");
  }
}

/** @brief Split "run <name>" and start the named test. */
static void dispatchRun(const char *name) {
  if (testHelpersEquals(name, "all")) {
    (void)startTest(START_TEST_ALL);
    return;
  }
  const ecu_test_t *entry = findByName(name);
  if (entry == NULL) {
    derr("Unknown test: '%s'. Send list for the registry.", name);
    return;
  }
  (void)startTest(entry->id);
}

static void dispatch(const char *line) {
  static const char run[] = "run ";
  size_t skip = 0U;
  while (line[skip] == ' ') {
    skip++;
  }
  const char *cmd = &line[skip];
  if (cmd[0] == '\0') {
    return;
  }

  if ((cmd[0] == '?') && (cmd[1] == '\0')) {
    printHelp();
    return;
  }
  if (testHelpersEquals(cmd, "help")) {
    printHelp();
    return;
  }
  if (testHelpersEquals(cmd, "list")) {
    printList();
    return;
  }
  if (testHelpersEquals(cmd, "stop")) {
    (void)stopTests();
    return;
  }
  if (testHelpersEquals(cmd, "skip")) {
    (void)stopTest();
    return;
  }

  size_t prefix = 0U;
  while ((run[prefix] != '\0') &&
         (testHelpersUpper(cmd[prefix]) == testHelpersUpper(run[prefix]))) {
    prefix++;
  }
  if (run[prefix] == '\0') {
    dispatchRun(&cmd[prefix]);
    return;
  }

  ecu_test_id_t requested = START_TEST_NONE;
  VP37Pump *pump = &getECUContext()->injectionPump;
  if (!testHelpersApplyCommand(pump, cmd, &requested)) {
    derr("Unknown command: '%s'. Send ? for help.", cmd);
    return;
  }
  if (requested != START_TEST_NONE) {
    (void)startTest(requested);
  }
}

void tickTestsHandleSerialLine(const char *line) {
  if (!s_initialized) {
    return;
  }
  const hal_status_t status = testHelpersQueueCommand(line);
  if (status == HAL_EBUSY) {
    derr("Test command pending; retry after acknowledgement");
  } else if (status == HAL_EINVAL) {
    derr("Test command rejected: empty or too long");
  } else {
    // Queued for the controller core.
  }
}

bool tickTests(void) {
  if (!s_initialized) {
    return false;
  }

  char line[VP37_CMD_BUF_SIZE];
  if (testHelpersTakeCommand(line)) {
    dispatch(line);
  }

  const ecu_test_t *entry = findById(s_active);
  if (entry == NULL) {
    return false;
  }

  bool finished = true;
  float demand = 0.0f;
  if (entry->step != NULL) {
    demand = entry->step(&finished);
  }
  const bool owned = entry->drivesDemand;
  if (owned) {
    (void)VP37_setPositionDemandPercentage(&getECUContext()->injectionPump,
                                           demand);
  }
  if (finished) {
    deactivate();
    if (s_sequenceRunning) {
      advanceSequence();
    }
  }
  return owned;
}

#else /* ECU_FUNCTIONAL_TESTS_ENABLED */

//=============================================================================
// Tests are not compiled in: the ECU keeps its normal demand source and no
// command can reach a fixture.
//=============================================================================

bool initTests(void) { return true; }

bool startTests(void) { return true; }

hal_status_t startTest(ecu_test_id_t test) {
  (void)test;
  return HAL_EUNSUPPORTED;
}

hal_status_t stopTest(void) { return HAL_EUNSUPPORTED; }

hal_status_t stopTests(void) { return HAL_EUNSUPPORTED; }

bool tickTests(void) { return false; }

void tickTestsHandleSerialLine(const char *line) { (void)line; }

const char *testsActiveName(void) { return NULL; }

uint32_t testsCyclicDelayMs(void) { return 0U; }

#endif /* ECU_FUNCTIONAL_TESTS_ENABLED */
