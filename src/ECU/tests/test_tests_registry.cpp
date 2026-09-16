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
  if (pump->adjustController != NULL) {
    hal_pid_controller_destroy(pump->adjustController);
    pump->adjustController = NULL;
  }
}

static VP37Pump *preparePump(void) {
  VP37Pump *pump = &getECUContext()->injectionPump;
  memset(pump, 0, sizeof(*pump));
  pump->adjustController = hal_pid_controller_create();
  pump->calibrationDone = true;
  pump->vp37Initialized = true;
  pump->VP37_ADJUST_MIN = 100;
  pump->VP37_ADJUST_MAX = 9100;
  pump->lastThrottle = -1.0f;
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
  TEST_ASSERT_FLOAT_WITHIN(.001f, -1.0f, pump->lastThrottle);

  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_CYCLIC));
  TEST_ASSERT_EQUAL_STRING("cyclic", testsActiveName());
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->lastThrottle);

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
  const float first = pump->lastThrottle;
  TEST_ASSERT_TRUE(first >= 0.0f);
  TEST_ASSERT_TRUE(first <= 100.0f);

  // A second position is drawn once the hold elapses.
  hal_mock_set_millis(1100U);
  TEST_ASSERT_TRUE(tickTests());
  const float second = pump->lastThrottle;
  TEST_ASSERT_TRUE(second >= 0.0f);
  TEST_ASSERT_TRUE(second <= 100.0f);

  // The seed is fixed, so restarting repeats the same draw.
  hal_mock_set_millis(1500U);
  TEST_ASSERT_EQUAL_INT(HAL_OK, startTest(START_TEST_RANDOM));
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, first, pump->lastThrottle);

  // The configured duration ends the test and releases the demand.
  hal_mock_set_millis(1500U + 2001U);
  TEST_ASSERT_TRUE(tickTests());
  TEST_ASSERT_FLOAT_WITHIN(.001f, 0.0f, pump->lastThrottle);
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
  TEST_ASSERT_FLOAT_WITHIN(.001f, 12.0f, pump->lastThrottle);

  // Listing and help leave the active test alone.
  console("list");
  console("?");
  TEST_ASSERT_EQUAL_STRING("manual", testsActiveName());
  console("stop");
  TEST_ASSERT_NULL(testsActiveName());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_registry_refuses_every_call_before_initialization);
  RUN_TEST(test_one_test_owns_the_demand_and_gives_it_back_on_stop);
  RUN_TEST(test_random_draws_a_bounded_and_repeatable_sequence);
  RUN_TEST(test_sequence_visits_every_sequenced_test_in_registry_order);
  RUN_TEST(test_skip_advances_the_sequence_and_stop_ends_it);
  RUN_TEST(test_console_starts_tests_by_name_and_keeps_parameters_on_letters);
  return UNITY_END();
}
