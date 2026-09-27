#ifndef T_TESTS
#define T_TESTS

#include <JaszczurHAL.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file tests.h
 * @brief Built-in functional tests: identifiers, lifecycle, the console entry
 * and the SerialConfigurator operations.
 *
 * The suite itself is the table in tests.c; the functions below are the
 * runner in tests_helpers.c.
 *
 * One build flag covers every test, its parameters, the console parser and the
 * VP37 RAM trace. With ECU_FUNCTIONAL_TESTS_ENABLED set to 0 the ECU compiles
 * and runs without any of it, and no console command can bring it back. The
 * normal demand source is selected by ECU_ENGINE_CONTROL_ENABLED and stays
 * available either way.
 */
#ifndef ECU_FUNCTIONAL_TESTS_ENABLED
#define ECU_FUNCTIONAL_TESTS_ENABLED 0
#endif

/** @brief Test selector for startTest() and the console "run" command. */
typedef enum {
  START_TEST_NONE = 0, /**< No test runs; the normal demand source drives. */
  START_TEST_DTC,      /**< One-shot diagnostic trouble code injection. */
  START_TEST_KV,       /**< One-shot key-value write and read-back. */
  START_TEST_CYCLIC,   /**< Deterministic 0-100-0 ramps over four step sizes. */
  START_TEST_RANDOM,   /**< Random positions, each held for a fixed time. */
  START_TEST_MANUAL,   /**< Demand held at the value last set by command S. */
  START_TEST_TOPSTEPS, /**< Staircase through the upper travel and back to 0. */
  START_TEST_TOPZERO,  /**< Same thresholds, each approached from rest. */
  START_TEST_POT,      /**< Hand-turned pot: tracked ramp to 100 %, hold,
                            return, rest. */
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
 * @brief Prepare the runner, the suite's fixtures and their parameters.
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
 * @note The test begins immediately and ends any running sequence. Selecting
 * another test first releases the old demand; restarting the same test keeps
 * the demand continuous.
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
 * @note The demand returns to the source chosen by ECU_ENGINE_CONTROL_ENABLED
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

struct sc_command_test_ops_s;

/**
 * @brief Functional tests as the SerialConfigurator service sees them.
 * @return Operations for sc_command_service_config_t::tests; a build without
 * tests returns operations with an empty catalog.
 * @note The operations run on the core that polls the serial session. Runs,
 * stops and skips are queued for the controller core; a test started this
 * way stops when the session ends, when SC_TEST_STATUS stays silent for
 * ECU_SC_TESTS_KEEPALIVE_MS, or, with ECU_SC_TESTS_RPM_INTERLOCK, once the
 * engine speed exceeds ECU_SC_TESTS_RPM_LIMIT.
 */
const struct sc_command_test_ops_s *testsScOps(void);

/**
 * @brief Report that the SerialConfigurator session ended.
 * @note Safe from either core. The controller core stops a test the
 * configurator started; console tests keep running.
 */
void testsScSessionEnded(void);

#ifdef __cplusplus
}
#endif

#endif
