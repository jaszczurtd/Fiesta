#include "dtcManager.h"
#include "ecuContext.h"
#include "ecuPersistence.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hal/storage/hal_eeprom.h"
#include "hal/storage/hal_kv.h"
#include "hardwareConfig.h"
#include "sensors.h"
#include "tests.h"
#include "tests_workers.h"
#include "unity.h"
#include "vp37_tuning.h"

#include "../../common/scDefinitions/sc_command_handlers.h"
#include "../../common/scDefinitions/sc_protocol.h"

#include <math.h>
#include <string.h>

#ifdef ECU_TEST_CLOCK_INTERLEAVING
static void (*s_clockHook)(void);
static bool s_finishManual;
extern "C" uint32_t __real_hal_millis(void);
extern "C" uint32_t __wrap_hal_millis(void) {
  const uint32_t now = __real_hal_millis();
  if (s_clockHook != nullptr) {
    void (*hook)(void) = s_clockHook;
    s_clockHook = nullptr;
    hook();
  }
  return now;
}
extern "C" float __real_testsWorkersManualStep(bool *finished);
extern "C" float __wrap_testsWorkersManualStep(bool *finished) {
  if (s_finishManual) {
    hal_mock_advance_millis(1U);
    *finished = true;
    return 0.0f;
  }
  return __real_testsWorkersManualStep(finished);
}
#endif

void setUp(void) {}

/** @brief Release the controller each fixture allocates, so runs stay clean. */
void tearDown(void) {
#ifdef ECU_TEST_CLOCK_INTERLEAVING
  s_clockHook = nullptr;
  s_finishManual = false;
#endif
  VP37Pump *pump = &getECUContext()->injectionPump;
  if (pump->pid.controller != NULL) {
    hal_pid_controller_destroy(pump->pid.controller);
    pump->pid.controller = NULL;
  }
}

static VP37Pump *preparePump(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  memset(pump, 0, sizeof(*pump));
  pump->pid.controller = hal_pid_controller_create();
  pump->feedback.calibrationDone = true;
  pump->vp37Initialized = true;
  pump->feedback.adjustMin = 100;
  pump->feedback.adjustMax = 9100;
  pump->demand.requestedPercent = -1.0f;
  hal_mock_set_millis(0U);
  return pump;
}

/** @brief Deliver one console line and let the controller core apply it. */
static void console(const char *line) {
  tickTestsHandleSerialLine(line);
  (void)tickTests();
}

/** @brief Advance simulated time until the active test changes or time runs
 * out. Returns the milliseconds spent. */
static uint32_t runUntilTestChanges(const char *expected, uint32_t limitMs) {
  for (uint32_t ms = 1U; ms <= limitMs; ms++) {
    hal_mock_set_millis(ms);
    (void)tickTests();
    const char *active = testsActiveName();
    const bool changed =
        expected == nullptr
            ? (active != nullptr)
            : (active == nullptr) || (strcmp(active, expected) != 0);
    if (changed) {
      return ms;
    }
  }
  return limitMs;
}

// The registry must refuse every call before initTests(); this has to run
// first, because initialization is a one-way step for the whole binary.
void test_registry_refuses_every_call_before_initialization(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, startTest(START_TEST_CYCLIC));
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, stopTest());
  TEST_ASSERT_EQUAL_INT(HAL_EUNINIT, stopTests());
  TEST_ASSERT_FALSE(tickTests());
  TEST_ASSERT_NULL(testsActiveName());
}

void test_one_test_owns_the_demand_and_gives_it_back_on_stop(void) {
  VP37Pump *pump = preparePump();
  TEST_ASSERT_TRUE(initTests());

  TEST_ASSERT_FALSE(tickTests());
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_FLOAT_WITHIN(.001f, -1.0f, pump->demand.requestedPercent);

  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_CYCLIC));
  TEST_ASSERT_EQUAL_STRING("cyclic", testsActiveName());
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.requestedPercent);

  TEST_ASSERT_EQUAL_INT(HAL_OK, stopTests());
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_FALSE(tickTests());
  TEST_ASSERT_EQUAL_UINT32(0U, testsCyclicDelayMs());

  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, startTest(START_TEST_COUNT));
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_NONE));
}

void test_random_draws_a_bounded_and_repeatable_sequence(void) {
  VP37Pump *pump = preparePump();
  TEST_ASSERT_TRUE(initTests());
  console("Y2");
  console("A1");

  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_RANDOM));
  TEST_ASSERT_TRUE(tickTests());
  const float first = pump->demand.requestedPercent;
  TEST_ASSERT_TRUE(first >= 0.0f);
  TEST_ASSERT_TRUE(first <= 100.0f);

  // A second position is drawn once the hold elapses.
  hal_mock_set_millis(1100U);
  TEST_ASSERT_TRUE(tickTests());
  const float second = pump->demand.requestedPercent;
  TEST_ASSERT_TRUE(second >= 0.0f);
  TEST_ASSERT_TRUE(second <= 100.0f);

  // The seed is fixed, so restarting repeats the same draw.
  hal_mock_set_millis(1500U);
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_RANDOM));
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, first, pump->demand.requestedPercent);

  // The configured duration ends the test and releases the demand.
  hal_mock_set_millis(1500U + 2001U);
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.requestedPercent);
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_FALSE(tickTests());
}

void test_sequence_visits_every_sequenced_test_in_registry_order(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  console("Z1");
  console("Y1");
  console("A1");

  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_ALL));
  // The one-shot storage checks run first and never own the demand.
  TEST_ASSERT_EQUAL_STRING("dtc", testsActiveName());
  TEST_ASSERT_FALSE(tickTests());
  TEST_ASSERT_EQUAL_STRING("kv", testsActiveName());
  TEST_ASSERT_FALSE(tickTests());
  TEST_ASSERT_EQUAL_STRING("cyclic", testsActiveName());

  // One pass over the four profiles is 24 full ramps; allow generous headroom.
  (void)runUntilTestChanges("cyclic", 120000U);
  TEST_ASSERT_EQUAL_STRING("random", testsActiveName());

  (void)runUntilTestChanges("random", 120000U);
  // manual is on request only, so the sequence skips it and ends after top.
  TEST_ASSERT_EQUAL_STRING("top", testsActiveName());

  // The staircases keep their default dwell and series here.
  (void)runUntilTestChanges("top", 120000U);
  TEST_ASSERT_EQUAL_STRING("topzero", testsActiveName());

  (void)runUntilTestChanges("topzero", 200000U);
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_FALSE(tickTests());
}

/** @brief Bring up an empty key-value store the way the ECU lays it out. */
static void prepareStorage(void) {
  hal_mock_eeprom_reset();
  hal_mock_eeprom_set_io_status(HAL_OK);
  hal_mock_eeprom_set_replace_fail_phase(HAL_MOCK_EEPROM_REPLACE_FAIL_NONE);
  hal_eeprom_init(HAL_EEPROM_FLASH, ECU_EEPROM_SIZE_BYTES, 0);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_init_ex(ECU_KV_BASE, ECU_KV_SIZE));
  // The state the ECU's own initialisation leaves behind.
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_read_through(true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_kv_set_auto_commit(true));
  TEST_ASSERT_EQUAL_INT(HAL_OK, ecuPersistenceInit());
  dtcManagerInit();
}

void test_kv_one_shot_counts_up_and_reports_the_store(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  prepareStorage();
  uint32_t counter = 0U;
  TEST_ASSERT_EQUAL_INT(
      HAL_ENOENT, hal_kv_get_u32_ex(TESTS_WORKERS_KV_COUNTER_KEY, &counter));

  // From the console, as on the bench: a one-shot that never owns the demand.
  console("run kv");
  TEST_ASSERT_NULL(testsActiveName());
  const tests_workers_kv_result_t *result = testsWorkersKvLastResult();
  TEST_ASSERT_TRUE(result->ok);
  TEST_ASSERT_EQUAL_UINT32(0U, result->before);
  TEST_ASSERT_EQUAL_UINT32(1U, result->after);
  TEST_ASSERT_EQUAL_INT(HAL_OK, result->write);
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_kv_get_u32_ex(TESTS_WORKERS_KV_COUNTER_KEY, &counter));
  TEST_ASSERT_EQUAL_UINT32(1U, counter);

  // The counter is what the next run reads back: two runs across a reset or
  // a power cycle prove the store persists. The report names the numbers.
  hal_mock_serial_reset();
  testsWorkersKvStart();
  TEST_ASSERT_NOT_NULL(strstr(hal_mock_deb_last_line(),
                              "TEST: KV counter 1 -> 2 write=HAL_OK keys="));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_kv_get_u32_ex(TESTS_WORKERS_KV_COUNTER_KEY, &counter));
  TEST_ASSERT_EQUAL_UINT32(2U, counter);
}

void test_kv_one_shot_names_a_refused_publication(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  prepareStorage();
  console("run kv");
  TEST_ASSERT_TRUE(testsWorkersKvLastResult()->ok);

  // The bank publication fails, as the flash coordinator refused it on the
  // bench for ten days: the report must say so instead of looking fine.
  hal_mock_eeprom_set_replace_fail_phase(
      HAL_MOCK_EEPROM_REPLACE_FAIL_AFTER_BODY);
  hal_mock_serial_reset();
  testsWorkersKvStart();
  const tests_workers_kv_result_t *result = testsWorkersKvLastResult();
  TEST_ASSERT_FALSE(result->ok);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, result->write);
  // Errors leave through the serial channel, as derr() does on the ECU.
  const char *line = hal_mock_serial_last_line();
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, "TEST: KV counter"), line);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, "FAILED"), line);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, "write=HAL_EIO"), line);
  hal_mock_eeprom_set_replace_fail_phase(HAL_MOCK_EEPROM_REPLACE_FAIL_NONE);
}

/** @brief Hold the mock clock at ms, run one tick and report the demand. */
static float tickAt(uint32_t ms) {
  hal_mock_set_millis(ms);
  (void)tickTests();
  return getECUContext()->injectionPump.demand.requestedPercent;
}

void test_top_steps_hold_every_setpoint_for_the_dwell(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_TOPSTEPS));

  // The series the bench expects, written out instead of read back from the
  // fixture, so a reordered or retuned staircase fails here.
  static const float setpoints[] = {75.0f, 85.5f, 90.5f, 95.0f, 0.0f};
  uint32_t ms = 0U;
  for (uint32_t series = 0U; series < TOP_STEPS_SERIES; series++) {
    for (size_t i = 0U; i < COUNTOF(setpoints); i++) {
      TEST_ASSERT_EQUAL_STRING("top", testsActiveName());
      TEST_ASSERT_FLOAT_WITHIN(0.001f, setpoints[i], tickAt(ms));
      // Still the same setpoint one millisecond before the dwell is over.
      TEST_ASSERT_FLOAT_WITHIN(0.001f, setpoints[i],
                               tickAt(ms + TOP_STEPS_DWELL_MS - 1U));
      ms += TOP_STEPS_DWELL_MS;
    }
  }

  // The last series releases the actuator and hands the demand back.
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tickAt(ms));
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_FALSE(tickTests());
}

void test_top_zero_returns_to_rest_between_thresholds(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_TOPZERO));

  // Same thresholds as the "top" ladder, but each one is reached from zero.
  static const float thresholds[] = {75.0f, 85.5f, 90.5f, 95.0f};
  uint32_t ms = 0U;
  for (uint32_t series = 0U; series < TOP_STEPS_SERIES; series++) {
    for (size_t i = 0U; i < COUNTOF(thresholds); i++) {
      TEST_ASSERT_EQUAL_STRING("topzero", testsActiveName());
      TEST_ASSERT_FLOAT_WITHIN(0.001f, thresholds[i], tickAt(ms));
      TEST_ASSERT_FLOAT_WITHIN(0.001f, thresholds[i],
                               tickAt(ms + TOP_ZERO_DWELL_MS - 1U));
      ms += TOP_ZERO_DWELL_MS;
      // The hard descent between thresholds, held just as long.
      TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tickAt(ms));
      TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f,
                               tickAt(ms + TOP_ZERO_DWELL_MS - 1U));
      ms += TOP_ZERO_DWELL_MS;
    }
  }

  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tickAt(ms));
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_FALSE(tickTests());
}

void test_pot_turns_to_full_demand_as_a_tracked_target_then_holds_and_returns(
    void) {
  VP37Pump *pump = preparePump();
  TEST_ASSERT_TRUE(initTests());
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_POT));

  // A quarter of a percent every millisecond at the default rate: the target
  // moves on every tick of the turn, so the controller tracks it instead of
  // treating it as a lone step.
  const float perMs = POT_RATE_PERCENT_PER_S_DEFAULT * 0.001f;
  const uint32_t turnMs = (uint32_t)(100.0f / perMs);
  uint32_t ms = 0U;
  for (uint32_t pass = 0U; pass < POT_PASSES; pass++) {
    TEST_ASSERT_EQUAL_STRING("pot", testsActiveName());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tickAt(ms));
    for (uint32_t t = 1U; t < turnMs; t++) {
      TEST_ASSERT_FLOAT_WITHIN(0.01f, perMs * (float)t, tickAt(ms + t));
      if (t > 1U) {
        TEST_ASSERT_TRUE(pump->demand.targetMoving);
      }
    }
    ms += turnMs;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, tickAt(ms));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, tickAt(ms + POT_HOLD_MS - 1U));
    ms += POT_HOLD_MS;
    // The turn back runs at the same rate and ends at rest.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, tickAt(ms));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f - perMs * 100.0f, tickAt(ms + 100U));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tickAt(ms + turnMs));
    ms += turnMs;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tickAt(ms + POT_REST_MS - 1U));
    ms += POT_REST_MS;
  }

  // The last rest ends the test and hands the demand back at zero.
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tickAt(ms));
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_FALSE(tickTests());
}

void test_pot_rate_command_is_bounded_and_reset_restores_the_default(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());

  console("X1=500");
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_POT));
  (void)tickAt(0U);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, tickAt(100U));

  // Out-of-range rates are refused and leave the rate alone. A restart
  // re-arms the turn at the clock it sees.
  console("X1=5");
  console("X1=5000");
  hal_mock_set_millis(200U);
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_POT));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tickAt(200U));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, tickAt(300U));

  // R restores the default rate.
  console("R");
  hal_mock_set_millis(400U);
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_POT));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tickAt(400U));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, tickAt(500U));
  console("stop");
  TEST_ASSERT_NULL(testsActiveName());
}

void test_skip_advances_the_sequence_and_stop_ends_it(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());

  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_ALL));
  // The two storage one-shots pass by themselves before the first profile.
  TEST_ASSERT_FALSE(tickTests());
  TEST_ASSERT_EQUAL_STRING("kv", testsActiveName());
  TEST_ASSERT_FALSE(tickTests());
  TEST_ASSERT_EQUAL_STRING("cyclic", testsActiveName());

  TEST_ASSERT_EQUAL_INT(HAL_OK, stopTest());
  TEST_ASSERT_EQUAL_STRING("random", testsActiveName());

  TEST_ASSERT_EQUAL_INT(HAL_OK, stopTests());
  TEST_ASSERT_NULL(testsActiveName());
  // The sequence is over, so stopping again changes nothing.
  TEST_ASSERT_EQUAL_INT(HAL_OK, stopTest());
  TEST_ASSERT_NULL(testsActiveName());
}

void test_console_starts_tests_by_name_and_keeps_parameters_on_letters(void) {
  VP37Pump *pump = preparePump();
  TEST_ASSERT_TRUE(initTests());

  console("run cyclic");
  TEST_ASSERT_EQUAL_STRING("cyclic", testsActiveName());
  console("stop");
  TEST_ASSERT_NULL(testsActiveName());

  // Case and leading spaces do not matter; an unknown name changes nothing.
  console("  RUN Random");
  TEST_ASSERT_EQUAL_STRING("random", testsActiveName());
  console("run nonsense");
  TEST_ASSERT_EQUAL_STRING("random", testsActiveName());
  console("stop");

  // A demand command starts the manual test, which stays out of the sequence.
  console("S12");
  TEST_ASSERT_EQUAL_STRING("manual", testsActiveName());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 12.0f, pump->demand.requestedPercent);

  // Listing and help leave the active test alone.
  console("list");
  console("?");
  TEST_ASSERT_EQUAL_STRING("manual", testsActiveName());
  console("stop");
  TEST_ASSERT_NULL(testsActiveName());
}

void test_upper_derivative_command_is_deferred_bounded_and_resettable(void) {
  VP37Pump *pump = preparePump();
  TEST_ASSERT_TRUE(initTests());
  VP37_setVP37PID(pump, .3f, .2f, .001f, false);
  hal_pid_controller_set_max_integral(pump->pid.controller, 100.0f);
  console("S90");
  const int32_t target = pump->demand.target;
  pump->output.finalPWM = 777;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_pid_controller_step_ex(pump->pid.controller, 100.0f, 8000.0f,
                                         .005f, 0.0f, &pump->pid.terms));
  const float integral = pump->pid.terms.integral;
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, integral);

  tickTestsHandleSerialLine("H0.002");
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.topKd);
  (void)tickTests();
  TEST_ASSERT_FLOAT_WITHIN(.000001f, .002f, pump->pid.topKd);
  TEST_ASSERT_EQUAL_INT32(target, pump->demand.target);
  TEST_ASSERT_EQUAL_INT32(777, pump->output.finalPWM);
  TEST_ASSERT_EQUAL_STRING("manual", testsActiveName());
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, hal_pid_controller_step_ex(pump->pid.controller, 0.0f, 8000.0f,
                                         .005f, 0.0f, &pump->pid.terms));
  TEST_ASSERT_EQUAL_FLOAT(integral, pump->pid.terms.integral);

  const char *invalid[] = {"H",    "H-0.001", "H0.01001", "Hnan",
                           "Hinf", "H1e99",   "H0.001x"};
  for (size_t i = 0U; i < COUNTOF(invalid); ++i) {
    console(invalid[i]);
    TEST_ASSERT_FLOAT_WITHIN(.000001f, .002f, pump->pid.topKd);
  }
  console("H0");
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->pid.topKd);
  console("h0.01");
  TEST_ASSERT_FLOAT_WITHIN(.000001f, .01f, pump->pid.topKd);
  TEST_ASSERT_FLOAT_WITHIN(.000001f, .001f, pump->pid.kd);
  console("P0.4");
  console("I0.3");
  TEST_ASSERT_FLOAT_WITHIN(.000001f, .001f, pump->pid.kd);
  TEST_ASSERT_FLOAT_WITHIN(.000001f, .01f, pump->pid.topKd);
  console("R");
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_TOP_KD, pump->pid.topKd);
  TEST_ASSERT_EQUAL_FLOAT(VP37_PID_KD, pump->pid.kd);
  console("stop");
}

void test_manual_command_keeps_fractional_demand_and_zero(void) {
  VP37Pump *pump = preparePump();
  TEST_ASSERT_TRUE(initTests());
  console("S50.5");
  TEST_ASSERT_EQUAL_STRING("manual", testsActiveName());
  TEST_ASSERT_EQUAL_FLOAT(50.5f, pump->demand.requestedPercent);
  TEST_ASSERT_EQUAL_INT32(4645, pump->demand.target);

  console("S101");
  TEST_ASSERT_EQUAL_FLOAT(50.5f, pump->demand.requestedPercent);
  TEST_ASSERT_EQUAL_INT32(4645, pump->demand.target);
  console("S0");
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->demand.requestedPercent);
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.target);
  console("stop");
  TEST_ASSERT_NULL(testsActiveName());
}

void test_repeated_manual_command_preserves_settled_target_and_ramp(void) {
  VP37Pump *pump = preparePump();
  TEST_ASSERT_TRUE(initTests());
  console("S95");
  const int32_t target = pump->demand.target;
  const uint32_t changedMs = pump->demand.targetChangedMs;
  pump->demand.desired = target;
  pump->demand.desiredPosition = (float)target;
  pump->pid.topDBlend = 1.0f;
  pump->pid.effectiveKd = .004f;
  hal_pid_controller_set_kd(pump->pid.controller, pump->pid.effectiveKd);
  for (uint32_t ms = 10U; ms <= 100U; ms += 10U) {
    hal_mock_set_millis(ms);
    console("S95");
    TEST_ASSERT_EQUAL_INT32(target, pump->demand.target);
    TEST_ASSERT_EQUAL_UINT32(changedMs, pump->demand.targetChangedMs);
    TEST_ASSERT_EQUAL_INT32(target, pump->demand.desired);
    TEST_ASSERT_EQUAL_FLOAT((float)target, pump->demand.desiredPosition);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, pump->pid.topDBlend);
    TEST_ASSERT_EQUAL_FLOAT(.004f,
                            hal_pid_controller_get_kd(pump->pid.controller));
  }
  TEST_ASSERT_TRUE(hal_millis_deadline_expired(pump->demand.targetChangedMs,
                                               VP37_TARGET_STABLE_MS));

  hal_mock_set_millis(105U);
  console("S10");
  TEST_ASSERT_EQUAL_FLOAT(10.0f, pump->demand.requestedPercent);
  TEST_ASSERT_EQUAL_INT32(1000, pump->demand.target);
  TEST_ASSERT_EQUAL_UINT32(105U, pump->demand.targetChangedMs);
  // Accepting another target must leave the running trajectory in place.
  TEST_ASSERT_EQUAL_INT32(target, pump->demand.desired);
  TEST_ASSERT_EQUAL_FLOAT((float)target, pump->demand.desiredPosition);
  console("stop");
}

void test_stopped_manual_command_hands_over_through_common_demand(void) {
  VP37Pump *pump = preparePump();
  TEST_ASSERT_TRUE(initTests());
  console("S95");
  const int32_t previousDesired = pump->demand.target;
  pump->demand.desired = previousDesired;
  pump->demand.desiredPosition = (float)previousDesired;
  hal_mock_set_millis(5U);
  console("stop");
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_FALSE(tickTests());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pump->demand.requestedPercent);
  TEST_ASSERT_EQUAL_INT32(pump->feedback.adjustMin, pump->demand.target);
  TEST_ASSERT_EQUAL_INT32(previousDesired, pump->demand.desired);

  // The normal source resumes through the same entry in the next iteration.
  hal_mock_set_millis(10U);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemandPercentage(pump, 10.25f));
  TEST_ASSERT_EQUAL_FLOAT(10.25f, pump->demand.requestedPercent);
  TEST_ASSERT_EQUAL_INT32(1022, pump->demand.target);
  TEST_ASSERT_EQUAL_UINT32(10U, pump->demand.targetChangedMs);
  TEST_ASSERT_EQUAL_INT32(previousDesired, pump->demand.desired);
  TEST_ASSERT_EQUAL_FLOAT((float)previousDesired, pump->demand.desiredPosition);
}

// ── SerialConfigurator surface ─────────────────────────────────────────────

static const sc_command_test_ops_t *scOps(void) { return testsScOps(); }

/** @brief Configurator status with the host's poll counted as contact. */
static sc_command_test_status_t scStatus(void) {
  sc_command_test_status_t status = {};
  scOps()->status(nullptr, &status);
  return status;
}

/** @brief Value of one progress key in a status, or INT32_MIN when absent. */
static int32_t statusField(const sc_command_test_status_t &status,
                           const char *key) {
  for (size_t i = 0U; i < status.field_count; i++) {
    if (strcmp(status.fields[i].key, key) == 0) {
      return status.fields[i].value;
    }
  }
  return INT32_MIN;
}

/** @brief Sensor values at rest: the engine speed reads zero. */
static void engineAtRest(void) { initSensors(); }

/** @brief Advance time in 1 ms ticks while the host keeps polling. */
static void runPolled(uint32_t fromMs, uint32_t toMs) {
  for (uint32_t ms = fromMs; ms <= toMs; ms++) {
    hal_mock_set_millis(ms);
    (void)tickTests();
    if ((ms % 250U) == 0U) {
      (void)scStatus();
    }
  }
}

void test_configurator_catalog_offers_only_supervisable_tests(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  const sc_command_test_ops_t *ops = scOps();

  // dtc leaves a fault behind, kv writes flash and manual never ends: none
  // of them is offered.
  static const struct {
    const char *name;
    bool inSequence;
    size_t params;
  } k_expected[] = {
      {SC_TEST_NAME_CYCLIC, true, 2U}, {SC_TEST_NAME_RANDOM, true, 2U},
      {SC_TEST_NAME_TOP, true, 2U},    {SC_TEST_NAME_TOPZERO, true, 2U},
      {SC_TEST_NAME_POT, false, 4U},
  };
  TEST_ASSERT_EQUAL_UINT(COUNTOF(k_expected), ops->count(nullptr));
  for (size_t i = 0U; i < COUNTOF(k_expected); i++) {
    sc_command_test_info_t info = {};
    TEST_ASSERT_TRUE(ops->info(nullptr, i, &info));
    TEST_ASSERT_EQUAL_STRING(k_expected[i].name, info.name);
    TEST_ASSERT_EQUAL(k_expected[i].inSequence, info.in_sequence);
    TEST_ASSERT_EQUAL_UINT(k_expected[i].params, info.param_count);
  }
  sc_command_test_info_t info = {};
  TEST_ASSERT_FALSE(ops->info(nullptr, COUNTOF(k_expected), &info));

  // Parameters come in table order with their unit, range and default.
  sc_command_test_param_t param = {};
  TEST_ASSERT_TRUE(ops->param_at(nullptr, 4U, 0U, &param));
  TEST_ASSERT_EQUAL_STRING(SC_TEST_PARAM_POT_RATE, param.id);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_POT, param.test);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_UNIT_PCT_PER_S, param.unit);
  TEST_ASSERT_EQUAL_INT32((int32_t)POT_RATE_PERCENT_PER_S_DEFAULT,
                          param.default_value);
  TEST_ASSERT_EQUAL_INT32(param.default_value, param.value);
  TEST_ASSERT_EQUAL_INT32(POT_RATE_PERCENT_PER_S_MIN, param.min);
  TEST_ASSERT_EQUAL_INT32(POT_RATE_PERCENT_PER_S_MAX, param.max);
  TEST_ASSERT_FALSE(ops->param_at(nullptr, 4U, 4U, &param));
  TEST_ASSERT_FALSE(ops->param_at(nullptr, 5U, 0U, &param));
  TEST_ASSERT_TRUE(ops->param(nullptr, SC_TEST_PARAM_TOP_DWELL, &param));
  TEST_ASSERT_EQUAL_INT32((int32_t)TOP_STEPS_DWELL_MS, param.value);
  TEST_ASSERT_FALSE(ops->param(nullptr, "no_such_param", &param));
}

void test_runtime_params_are_bounded_shared_with_the_console_and_reset(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  const sc_command_test_ops_t *ops = scOps();

  TEST_ASSERT_EQUAL_INT(
      HAL_OK, ops->set_param(nullptr, SC_TEST_PARAM_CYCLIC_PASSES, 7));
  TEST_ASSERT_EQUAL_INT32(7, testsHelpersParam(TEST_PARAM_CYCLIC_PASSES));
  // Console letter Z writes the same value the configurator reads.
  console("Z3");
  sc_command_test_param_t param = {};
  TEST_ASSERT_TRUE(ops->param(nullptr, SC_TEST_PARAM_CYCLIC_PASSES, &param));
  TEST_ASSERT_EQUAL_INT32(3, param.value);

  // Out of range and unknown ids change nothing.
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, ops->set_param(nullptr, SC_TEST_PARAM_CYCLIC_PASSES, 0));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        ops->set_param(nullptr, SC_TEST_PARAM_CYCLIC_PASSES,
                                       (int32_t)CYCLIC_PASSES_MAX + 1));
  TEST_ASSERT_EQUAL_INT32(3, testsHelpersParam(TEST_PARAM_CYCLIC_PASSES));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, ops->set_param(nullptr, "missing", 1));

  // The console reaches every parameter by id, too.
  console("set " SC_TEST_PARAM_POT_HOLD " 1500");
  TEST_ASSERT_EQUAL_INT32(1500, testsHelpersParam(TEST_PARAM_POT_HOLD));
  console("set " SC_TEST_PARAM_POT_HOLD " 10");
  TEST_ASSERT_EQUAL_INT32(1500, testsHelpersParam(TEST_PARAM_POT_HOLD));

  // R, like a restart, brings every default back.
  console("R");
  for (size_t i = 0U; i < (size_t)TEST_PARAM_COUNT; i++) {
    TEST_ASSERT_EQUAL_INT32(testsHelpersParamDesc((test_param_id_t)i)->def,
                            testsHelpersParam((test_param_id_t)i));
  }
}

void test_runtime_params_shape_the_staircase(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  console("set " SC_TEST_PARAM_TOP_SERIES " 1");
  console("set " SC_TEST_PARAM_TOP_DWELL " 200");

  // Five setpoints of 200 ms, one series: the staircase ends after a second.
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_TOPSTEPS));
  const uint32_t endedMs = runUntilTestChanges("top", 20000U);
  TEST_ASSERT_UINT32_WITHIN(2U, 1000U, endedMs);
}

void test_configurator_run_is_queued_and_reports_progress(void) {
  VP37Pump *pump = preparePump();
  TEST_ASSERT_TRUE(initTests());
  const sc_command_test_ops_t *ops = scOps();
  engineAtRest();

  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, ops->run(nullptr, "dtc"));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, ops->run(nullptr, "kv"));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, ops->run(nullptr, "manual"));
  TEST_ASSERT_EQUAL_INT(HAL_ENOENT, ops->run(nullptr, "missing"));

  TEST_ASSERT_EQUAL_INT(HAL_OK, ops->run(nullptr, SC_TEST_NAME_CYCLIC));
  // The controller core takes the request on its next tick.
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_EQUAL_INT(HAL_EBUSY, ops->stop(nullptr));
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_CYCLIC, testsActiveName());

  // Parked at the top of the usable stroke: position reads 100.0 %.
  pump->feedback.position = VP37_getPositionDemandMaxValue(pump);
  hal_mock_set_millis(40U);
  (void)tickTests();
  const sc_command_test_status_t status = scStatus();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_CYCLIC, status.active);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_SOURCE_SC, status.source);
  TEST_ASSERT_EQUAL_UINT8(0U, status.seq_count);
  TEST_ASSERT_EQUAL_UINT32(40U, status.elapsed_ms);
  // Profile A steps 1 % every 4 ms: 250 %/s.
  TEST_ASSERT_EQUAL_INT32(1, statusField(status, SC_TEST_FIELD_PROFILE));
  TEST_ASSERT_EQUAL_INT32(250, statusField(status, SC_TEST_FIELD_RATE));
  TEST_ASSERT_EQUAL_INT32(1, statusField(status, SC_TEST_FIELD_CYCLE));
  TEST_ASSERT_EQUAL_INT32((int32_t)CYCLIC_FULL_CYCLES,
                          statusField(status, SC_TEST_FIELD_CYCLES));
  TEST_ASSERT_EQUAL_INT32(1, statusField(status, SC_TEST_FIELD_PASS));
  TEST_ASSERT_EQUAL_INT32((int32_t)CYCLIC_PASSES_DEFAULT,
                          statusField(status, SC_TEST_FIELD_PASSES));
  // Demand and position are percent of the usable stroke, times ten.
  TEST_ASSERT_TRUE(status.drive_valid);
  TEST_ASSERT_EQUAL_INT32(
      (int32_t)lroundf(pump->demand.requestedPercent * 10.0f),
      status.demand_x10);
  TEST_ASSERT_EQUAL_INT32(1000, status.position_x10);

  TEST_ASSERT_EQUAL_INT(HAL_OK, ops->stop(nullptr));
  TEST_ASSERT_FALSE(tickTests());
  const sc_command_test_status_t stopped = scStatus();
  TEST_ASSERT_NULL(stopped.active);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_CYCLIC, stopped.last);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_STOPPED, stopped.result);
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.requestedPercent);
}

// The periodic status refresh shares the loop that releases every control
// step, so it runs only while the configurator polls; starts and stops always
// publish.
void test_configurator_status_refreshes_only_while_polled(void) {
  VP37Pump *pump = preparePump();
  hal_mock_set_millis(1000U);
  TEST_ASSERT_TRUE(initTests());
  engineAtRest();
  pump->feedback.position = VP37_getPositionDemandMinValue(pump);
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_MANUAL));

  // Nobody polls: the drive moves, the published status stays as started.
  pump->feedback.position = VP37_getPositionDemandMaxValue(pump);
  hal_mock_set_millis(1100U);
  (void)tickTests();
  // The first poll reads that snapshot and counts as contact.
  TEST_ASSERT_EQUAL_INT32(0, scStatus().position_x10);
  // One refresh interval later the poll sees the drive as it is.
  hal_mock_set_millis(1120U);
  (void)tickTests();
  TEST_ASSERT_EQUAL_INT32(1000, scStatus().position_x10);

  // Once the host is quiet for the keepalive time, the refresh stops again.
  pump->feedback.position = VP37_getPositionDemandMinValue(pump);
  hal_mock_set_millis(1120U + ECU_SC_TESTS_KEEPALIVE_MS);
  (void)tickTests();
  TEST_ASSERT_EQUAL_INT32(1000, scStatus().position_x10);
  TEST_ASSERT_EQUAL_INT(HAL_OK, stopTests());
  TEST_ASSERT_NULL(scStatus().active);
}

#ifdef ECU_TEST_CLOCK_INTERLEAVING
static void pollFromOtherCore(void) {
  hal_mock_advance_millis(1U);
  (void)scStatus();
}

void test_configurator_contact_newer_than_clock_does_not_stop_test(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  engineAtRest();
  TEST_ASSERT_EQUAL_INT(HAL_OK, scOps()->run(nullptr, SC_TEST_NAME_CYCLIC));
  (void)tickTests();
  hal_mock_set_millis(1000U);
  (void)scStatus();
  s_clockHook = pollFromOtherCore;
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_CYCLIC, testsActiveName());
  TEST_ASSERT_EQUAL_INT(HAL_OK, stopTests());
}

void test_finished_test_keeps_status_refresh_interval(void) {
  VP37Pump *pump = preparePump();
  hal_mock_set_millis(1000U);
  TEST_ASSERT_TRUE(initTests());
  engineAtRest();
  pump->feedback.position = VP37_getPositionDemandMinValue(pump);
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_MANUAL));
  (void)scStatus();
  s_finishManual = true;
  (void)tickTests(); // The worker crosses a millisecond before it finishes.
  TEST_ASSERT_NULL(testsActiveName());
  pump->feedback.position = VP37_getPositionDemandMaxValue(pump);
  hal_mock_set_millis(1020U); // Only 19 ms since the stop publication.
  (void)tickTests();
  TEST_ASSERT_EQUAL_INT32(0, scStatus().position_x10);
  hal_mock_set_millis(1021U);
  (void)tickTests();
  TEST_ASSERT_EQUAL_INT32(1000, scStatus().position_x10);
}
#endif

// Each entry binds its own progress: the staircases share keys, so their
// series counts are set apart to tell a swapped binding.
void test_configurator_tests_report_their_own_progress(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  const sc_command_test_ops_t *ops = scOps();
  engineAtRest();
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        ops->set_param(nullptr, SC_TEST_PARAM_TOP_SERIES, 3));
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, ops->set_param(nullptr, SC_TEST_PARAM_TOPZERO_SERIES, 4));

  static const struct {
    const char *name;
    const char *key;
    int32_t value;
  } k_expected[] = {
      {SC_TEST_NAME_CYCLIC, SC_TEST_FIELD_PASSES,
       (int32_t)CYCLIC_PASSES_DEFAULT},
      {SC_TEST_NAME_RANDOM, SC_TEST_FIELD_LEFT_S,
       (int32_t)RANDOM_DURATION_S_DEFAULT},
      {SC_TEST_NAME_TOP, SC_TEST_FIELD_SERIES_COUNT, 3},
      {SC_TEST_NAME_TOPZERO, SC_TEST_FIELD_SERIES_COUNT, 4},
      {SC_TEST_NAME_POT, SC_TEST_FIELD_RATE,
       (int32_t)POT_RATE_PERCENT_PER_S_DEFAULT},
  };
  for (size_t i = 0U; i < COUNTOF(k_expected); i++) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, ops->run(nullptr, k_expected[i].name));
    TEST_ASSERT_TRUE(tickTests());
    const sc_command_test_status_t status = scStatus();
    TEST_ASSERT_EQUAL_STRING(k_expected[i].name, status.active);
    TEST_ASSERT_EQUAL_INT32(k_expected[i].value,
                            statusField(status, k_expected[i].key));
    TEST_ASSERT_EQUAL_INT(HAL_OK, ops->stop(nullptr));
    TEST_ASSERT_FALSE(tickTests());
  }

  // The manual hold has nothing to count and publishes no values.
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_MANUAL));
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_EQUAL_UINT(0U, scStatus().field_count);
  TEST_ASSERT_EQUAL_INT(HAL_OK, stopTests());
}

void test_configurator_sequence_runs_only_its_own_tests(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  const sc_command_test_ops_t *ops = scOps();
  engineAtRest();

  const uint32_t runsBefore = scStatus().runs;
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops->run(nullptr, SC_TEST_SEQUENCE));
  // Neither dtc nor kv is a configurator test, so the sequence opens with
  // cyclic, which takes the demand at once.
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_CYCLIC, testsActiveName());
  sc_command_test_status_t status = scStatus();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_SOURCE_SC, status.source);
  TEST_ASSERT_EQUAL_UINT8(1U, status.seq_index);
  TEST_ASSERT_EQUAL_UINT8(4U, status.seq_count);
  TEST_ASSERT_NULL(status.last);
  TEST_ASSERT_EQUAL_UINT32(runsBefore + 1U, status.runs);

  // Skip moves on inside the sequence, stop ends it.
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops->skip(nullptr));
  (void)tickTests();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_RANDOM, testsActiveName());
  status = scStatus();
  TEST_ASSERT_EQUAL_UINT8(2U, status.seq_index);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_CYCLIC, status.last);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_STOPPED, status.result);
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops->stop(nullptr));
  (void)tickTests();
  TEST_ASSERT_NULL(testsActiveName());
  status = scStatus();
  TEST_ASSERT_NULL(status.active);
  TEST_ASSERT_EQUAL_UINT8(0U, status.seq_count);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_RANDOM, status.last);

  // Skip with nothing running is harmless.
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops->skip(nullptr));
  TEST_ASSERT_FALSE(tickTests());
  TEST_ASSERT_NULL(testsActiveName());
}

void test_configurator_test_stops_when_the_host_goes_quiet(void) {
  VP37Pump *pump = preparePump();
  TEST_ASSERT_TRUE(initTests());
  engineAtRest();
  TEST_ASSERT_EQUAL_INT(HAL_OK, scOps()->run(nullptr, SC_TEST_NAME_POT));
  TEST_ASSERT_TRUE(tickTests());

  // Polling keeps it alive well past the keepalive time.
  runPolled(1U, 3U * ECU_SC_TESTS_KEEPALIVE_MS);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_POT, testsActiveName());

  // Silence for the keepalive time stops it and releases the demand.
  const uint32_t lastPollMs = 3U * ECU_SC_TESTS_KEEPALIVE_MS;
  hal_mock_set_millis(lastPollMs + ECU_SC_TESTS_KEEPALIVE_MS - 1U);
  (void)tickTests();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_POT, testsActiveName());
  hal_mock_set_millis(lastPollMs + ECU_SC_TESTS_KEEPALIVE_MS);
  (void)tickTests();
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->demand.requestedPercent);
  const sc_command_test_status_t status = scStatus();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_POT, status.last);
  TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_HOST_LOST, status.result);

  // A console test belongs to the bench operator and needs no host.
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_POT));
  hal_mock_set_millis(lastPollMs + 4U * ECU_SC_TESTS_KEEPALIVE_MS);
  (void)tickTests();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_POT, testsActiveName());
}

void test_configurator_test_stops_when_the_session_ends(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  engineAtRest();

  // Console test: a session ending on the same port is not its business.
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_CYCLIC));
  testsScSessionEnded();
  (void)tickTests();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_CYCLIC, testsActiveName());

  TEST_ASSERT_EQUAL_INT(HAL_OK, scOps()->run(nullptr, SC_TEST_NAME_CYCLIC));
  (void)tickTests();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_SOURCE_SC, scStatus().source);
  testsScSessionEnded();
  (void)tickTests();
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_SESSION_END, scStatus().result);
}

void test_configurator_start_follows_the_engine_speed_interlock(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());
  const sc_command_test_ops_t *ops = scOps();
  engineAtRest();

  setGlobalValue(F_RPM, 900.0f);
#if ECU_SC_TESTS_RPM_INTERLOCK
  TEST_ASSERT_EQUAL_INT(HAL_EPERM, ops->run(nullptr, SC_TEST_NAME_CYCLIC));
  TEST_ASSERT_EQUAL_INT(HAL_EPERM, ops->run(nullptr, SC_TEST_SEQUENCE));
  (void)tickTests();
  TEST_ASSERT_NULL(testsActiveName());

  // Started at rest, stopped once the engine turns.
  setGlobalValue(F_RPM, 0.0f);
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops->run(nullptr, SC_TEST_NAME_CYCLIC));
  (void)tickTests();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_CYCLIC, testsActiveName());
  setGlobalValue(F_RPM, 900.0f);
  (void)tickTests();
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_EQUAL_STRING(SC_TEST_RESULT_ENGINE_RUNNING, scStatus().result);
#else
  // The bench build turns the interlock off: a generator drives the RPM.
  TEST_ASSERT_EQUAL_INT(HAL_OK, ops->run(nullptr, SC_TEST_NAME_CYCLIC));
  (void)tickTests();
  (void)tickTests();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_CYCLIC, testsActiveName());
#endif

  // Console tests are never interlocked.
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_RANDOM));
  (void)tickTests();
  TEST_ASSERT_EQUAL_STRING(SC_TEST_NAME_RANDOM, testsActiveName());
  setGlobalValue(F_RPM, 0.0f);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_registry_refuses_every_call_before_initialization);
  RUN_TEST(test_one_test_owns_the_demand_and_gives_it_back_on_stop);
  RUN_TEST(test_random_draws_a_bounded_and_repeatable_sequence);
  RUN_TEST(test_sequence_visits_every_sequenced_test_in_registry_order);
  RUN_TEST(test_kv_one_shot_counts_up_and_reports_the_store);
  RUN_TEST(test_kv_one_shot_names_a_refused_publication);
  RUN_TEST(test_top_steps_hold_every_setpoint_for_the_dwell);
  RUN_TEST(test_top_zero_returns_to_rest_between_thresholds);
  RUN_TEST(
      test_pot_turns_to_full_demand_as_a_tracked_target_then_holds_and_returns);
  RUN_TEST(test_pot_rate_command_is_bounded_and_reset_restores_the_default);
  RUN_TEST(test_skip_advances_the_sequence_and_stop_ends_it);
  RUN_TEST(test_console_starts_tests_by_name_and_keeps_parameters_on_letters);
  RUN_TEST(test_upper_derivative_command_is_deferred_bounded_and_resettable);
  RUN_TEST(test_manual_command_keeps_fractional_demand_and_zero);
  RUN_TEST(test_repeated_manual_command_preserves_settled_target_and_ramp);
  RUN_TEST(test_stopped_manual_command_hands_over_through_common_demand);
  RUN_TEST(test_configurator_catalog_offers_only_supervisable_tests);
  RUN_TEST(test_runtime_params_are_bounded_shared_with_the_console_and_reset);
  RUN_TEST(test_runtime_params_shape_the_staircase);
  RUN_TEST(test_configurator_run_is_queued_and_reports_progress);
  RUN_TEST(test_configurator_status_refreshes_only_while_polled);
#ifdef ECU_TEST_CLOCK_INTERLEAVING
  RUN_TEST(test_configurator_contact_newer_than_clock_does_not_stop_test);
  RUN_TEST(test_finished_test_keeps_status_refresh_interval);
#endif
  RUN_TEST(test_configurator_tests_report_their_own_progress);
  RUN_TEST(test_configurator_sequence_runs_only_its_own_tests);
  RUN_TEST(test_configurator_test_stops_when_the_host_goes_quiet);
  RUN_TEST(test_configurator_test_stops_when_the_session_ends);
  RUN_TEST(test_configurator_start_follows_the_engine_speed_interlock);
  return UNITY_END();
}
