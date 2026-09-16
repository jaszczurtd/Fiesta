#ifndef T_TESTS
#define T_TESTS

#include <JaszczurHAL.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file tests.h
 * @brief Built-in functional tests: identifiers, lifecycle and console entry.
 *
 * One build flag covers every test, its parameters, the console parser and the
 * VP37 RAM trace. With ECU_FUNCTIONAL_TESTS_ENABLED set to 0 the ECU compiles
 * and runs without any of it, and no console command can bring it back. The
 * normal demand source is selected by VP37_ENGINE_OPERATION_MODE and stays
 * available either way.
 */
#ifndef ECU_FUNCTIONAL_TESTS_ENABLED
#define ECU_FUNCTIONAL_TESTS_ENABLED 0
#endif

/** @brief Test selector for startTest() and the console "run" command. */
typedef enum {
  START_TEST_NONE = 0, /**< No test runs; the normal demand source drives. */
  START_TEST_DTC,      /**< One-shot diagnostic trouble code injection. */
  START_TEST_CYCLIC,   /**< Deterministic 0-100-0 ramps over four step sizes. */
  START_TEST_RANDOM,   /**< Random positions, each held for a fixed time. */
  START_TEST_MANUAL,   /**< Demand held at the value last set by command S. */
  START_TEST_COUNT,    /**< Number of registered tests. */
  START_TEST_ALL       /**< Every sequenced test, one after another. */
} ecu_test_id_t;

/**
 * @brief Test started by startTests() without a console command.
 *
 * START_TEST_NONE waits for the console, START_TEST_ALL runs the whole
 * sequence unattended, and a single identifier runs just that test.
 */
#ifndef ECU_FUNCTIONAL_TESTS_AUTOSTART
#define ECU_FUNCTIONAL_TESTS_AUTOSTART START_TEST_NONE
#endif

/**
 * @brief Prepare the test registry and its fixtures.
 * @return True when initialization finished; false when a resource is missing.
 * @note Call once on core 0 before the control loop runs.
 */
bool initTests(void);

/**
 * @brief Start the test selected by ECU_FUNCTIONAL_TESTS_AUTOSTART.
 * @return True when the boot hook finished, including the case where no test
 * is configured to start.
 * @note This is the unattended entry: a build can run the full sequence from
 * power-up without anyone sending a command.
 */
bool startTests(void);

/**
 * @brief Start one test, or the full sequence with START_TEST_ALL.
 * @param test Identifier to start; START_TEST_NONE only stops what runs.
 * @return HAL_OK, HAL_EINVAL for an unknown identifier, HAL_EUNINIT before
 * initTests(), or HAL_EUNSUPPORTED when tests are not compiled in.
 * @note The test begins immediately. Any running test and any running
 * sequence are stopped first, and the demand passes to the new test.
 */
hal_status_t startTest(ecu_test_id_t test);

/**
 * @brief Stop the running test and let a running sequence continue.
 * @return HAL_OK, HAL_EUNINIT before initTests(), or HAL_EUNSUPPORTED when
 * tests are not compiled in.
 */
hal_status_t stopTest(void);

/**
 * @brief Stop the running test and end any sequence.
 * @return HAL_OK, HAL_EUNINIT before initTests(), or HAL_EUNSUPPORTED when
 * tests are not compiled in.
 * @note The demand returns to the source chosen by VP37_ENGINE_OPERATION_MODE
 * after the actuator has been commanded to zero.
 */
hal_status_t stopTests(void);

/**
 * @brief Execute one step of the running test and apply queued commands.
 * @return True while a test owns the actuator demand, so the caller leaves the
 * normal demand source alone.
 * @note Runs on the core that owns the VP37 state, under its mutex.
 */
bool tickTests(void);

/**
 * @brief Queue one console line for the test layer.
 * @param line NUL-terminated command line without the trailing CR or LF.
 * @return None.
 * @note Wired as the unknown-line callback of the serial session, so the
 * bootstrap protocol sees every line first. The line is applied later, on the
 * core that owns the controller. A no-op when tests are not compiled in.
 */
void tickTestsHandleSerialLine(const char *line);

/**
 * @brief Console name of the running test.
 * @return Name, or NULL when no test runs or tests are not compiled in.
 */
const char *testsActiveName(void);

/**
 * @brief Step delay of the running cyclic test.
 * @return Delay in milliseconds, or zero when the cyclic test is not running.
 */
uint32_t testsCyclicDelayMs(void);

#ifdef __cplusplus
}
#endif

#endif
