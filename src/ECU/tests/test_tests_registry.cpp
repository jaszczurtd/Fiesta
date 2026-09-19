#include "ecuContext.h"
#include "hal/impl/.mock/hal_mock.h"
#include "test_helpers.h"
#include "tests.h"
#include "unity.h"
#include "vp37.h"

#include <string.h>

void setUp(void) {}

/** @brief Release the controller each fixture allocates, so runs stay clean. */
void tearDown(void) {
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
  // The one-shot injection runs first and never owns the demand.
  TEST_ASSERT_EQUAL_STRING("dtc", testsActiveName());
  TEST_ASSERT_FALSE(tickTests());
  TEST_ASSERT_EQUAL_STRING("cyclic", testsActiveName());

  // One pass over the four profiles is 24 full ramps; allow generous headroom.
  (void)runUntilTestChanges("cyclic", 120000U);
  TEST_ASSERT_EQUAL_STRING("random", testsActiveName());

  (void)runUntilTestChanges("random", 120000U);
  // manual is on request only, so the sequence ends after random.
  TEST_ASSERT_NULL(testsActiveName());
  TEST_ASSERT_FALSE(tickTests());
}

void test_skip_advances_the_sequence_and_stop_ends_it(void) {
  (void)preparePump();
  TEST_ASSERT_TRUE(initTests());

  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_ALL));
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
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_setPositionDemand(pump, 10.25f));
  TEST_ASSERT_EQUAL_FLOAT(10.25f, pump->demand.requestedPercent);
  TEST_ASSERT_EQUAL_INT32(1022, pump->demand.target);
  TEST_ASSERT_EQUAL_UINT32(10U, pump->demand.targetChangedMs);
  TEST_ASSERT_EQUAL_INT32(previousDesired, pump->demand.desired);
  TEST_ASSERT_EQUAL_FLOAT((float)previousDesired, pump->demand.desiredPosition);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_registry_refuses_every_call_before_initialization);
  RUN_TEST(test_one_test_owns_the_demand_and_gives_it_back_on_stop);
  RUN_TEST(test_random_draws_a_bounded_and_repeatable_sequence);
  RUN_TEST(test_sequence_visits_every_sequenced_test_in_registry_order);
  RUN_TEST(test_skip_advances_the_sequence_and_stop_ends_it);
  RUN_TEST(test_console_starts_tests_by_name_and_keeps_parameters_on_letters);
  RUN_TEST(test_upper_derivative_command_is_deferred_bounded_and_resettable);
  RUN_TEST(test_manual_command_keeps_fractional_demand_and_zero);
  RUN_TEST(test_repeated_manual_command_preserves_settled_target_and_ramp);
  RUN_TEST(test_stopped_manual_command_hands_over_through_common_demand);
  return UNITY_END();
}
