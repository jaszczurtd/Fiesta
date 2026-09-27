#ifndef T_TESTS_HELPERS
#define T_TESTS_HELPERS

#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file tests_helpers.h
 * @brief Everything around the suite of tests.c that is not a test itself:
 * the runtime parameters, the entry format of the suite and the runner behind
 * tests.h, which owns the console, the sequence and the configurator surface.
 * What the tests do lives in tests_workers.h.
 *
 * Everything here follows ECU_FUNCTIONAL_TESTS_ENABLED: with tests disabled
 * tests_helpers.c only provides the idle stubs of tests.h and nothing below
 * is declared.
 */
#if ECU_FUNCTIONAL_TESTS_ENABLED

/** @brief Bench highlight colour. Each escape is a literal of its own, so
 * the octal sequence ends with it and needs no macro parameter. */
#define TEST_HIGHLIGHT_ON                                                      \
  "\033"                                                                       \
  "[33m"
#define TEST_HIGHLIGHT_OFF                                                     \
  "\033"                                                                       \
  "[0m"

/** @brief Longest console line the test layer accepts. */
#define VP37_CMD_BUF_SIZE 64U
/** @brief Bench ceiling for the integral cap set by command L. */
#define VP37_BENCH_INTEGRAL_LIMIT_MAX 360.0f

/** @brief Full 0-100-0 cycles per profile, by default. */
#ifndef CYCLIC_FULL_CYCLES
#define CYCLIC_FULL_CYCLES 6U
#endif
#define CYCLIC_FULL_CYCLES_MAX 50U
/** @brief Passes over all four profiles before the cyclic test finishes. */
#define CYCLIC_PASSES_DEFAULT 4U
/** @brief Bench ceiling for command Z. */
#define CYCLIC_PASSES_MAX 100U

/** @brief Total run time of the random test, in seconds. */
#define RANDOM_DURATION_S_DEFAULT 300U
/** @brief Time one drawn position is held, in seconds. */
#define RANDOM_HOLD_S_DEFAULT 5U
/** @brief Bench ceilings for commands Y and A. */
#define RANDOM_DURATION_S_MAX 3600U
#define RANDOM_HOLD_S_MAX 120U
/** @brief Time one step of the upper staircase is held, in milliseconds. */
#ifndef TOP_STEPS_DWELL_MS
#define TOP_STEPS_DWELL_MS 2000U
#endif
/**
 * @brief Time one half-step of the from-rest staircase is held, in
 * milliseconds.
 *
 * Longer than the ladder's step on purpose: a threshold reached from rest is
 * a full-travel excursion and the actuator is still arriving two seconds in,
 * so a shorter hold measures how long the approach takes rather than whether
 * the position is stable.
 */
#ifndef TOP_ZERO_DWELL_MS
#define TOP_ZERO_DWELL_MS 3000U
#endif
/** @brief Series the upper staircases repeat before they finish. */
#ifndef TOP_STEPS_SERIES
#define TOP_STEPS_SERIES 5U
#endif
/** @brief Bounds of the staircase dwell and series parameters. */
#define TOP_DWELL_MS_MIN 200U
#define TOP_DWELL_MS_MAX 20000U
#define TOP_SERIES_MAX 50U

/**
 * @brief Demand rate of the pot test, in percent per second.
 *
 * A hand turns the driver's pot to its stop in under half a second and the
 * application publishes that demand every control loop, so the target keeps
 * changing and the position ramp tracks it: full rate, full motion assist
 * and no arrival brake, unlike the lone step of command S. The default stays
 * below the demand slew on purpose, so the ramp follows the pot to the top.
 */
#define POT_RATE_PERCENT_PER_S_DEFAULT 250.0f
/** @brief Bench bounds for X1, the pot rate. */
#define POT_RATE_PERCENT_PER_S_MIN 10U
#define POT_RATE_PERCENT_PER_S_MAX 2000U
/** @brief Time the pot test holds full demand, in milliseconds. */
#ifndef POT_HOLD_MS
#define POT_HOLD_MS 12000U
#endif
/** @brief Rest at zero between passes of the pot test, in milliseconds. */
#ifndef POT_REST_MS
#define POT_REST_MS 5000U
#endif
/** @brief Passes (turn up, hold, turn down, rest) before the pot test ends. */
#ifndef POT_PASSES
#define POT_PASSES 3U
#endif
/** @brief Bounds of the pot hold, rest and passes parameters. */
#define POT_PHASE_MS_MIN 500U
#define POT_PHASE_MS_MAX 60000U
#define POT_PASSES_MAX 20U

/**
 * @brief Runtime parameters of the tests, in the order the configurator
 * lists them. Each starts at its compile-time default, and a restart or
 * command R brings the default back.
 */
typedef enum {
  TEST_PARAM_CYCLIC_PASSES = 0,
  TEST_PARAM_CYCLIC_CYCLES,
  TEST_PARAM_RANDOM_DURATION,
  TEST_PARAM_RANDOM_HOLD,
  TEST_PARAM_TOP_DWELL,
  TEST_PARAM_TOP_SERIES,
  TEST_PARAM_TOPZERO_DWELL,
  TEST_PARAM_TOPZERO_SERIES,
  TEST_PARAM_POT_RATE,
  TEST_PARAM_POT_HOLD,
  TEST_PARAM_POT_REST,
  TEST_PARAM_POT_PASSES,
  TEST_PARAM_COUNT
} test_param_id_t;

/** @brief Fixed description of one runtime parameter. */
typedef struct {
  const char *id;     /**< Wire id, SC_TEST_PARAM_*. */
  ecu_test_id_t test; /**< Test that reads it. */
  const char *unit;   /**< Unit token, SC_TEST_UNIT_*. */
  int32_t min;
  int32_t max;
  int32_t def; /**< Value after a restart or command R. */
} test_param_desc_t;

/**
 * @brief Description of one runtime parameter.
 * @param param Parameter below TEST_PARAM_COUNT.
 * @return The table row, or NULL for an index out of range.
 */
const test_param_desc_t *testsHelpersParamDesc(test_param_id_t param);

/**
 * @brief Value of one runtime parameter; safe from either core.
 * @param param Parameter below TEST_PARAM_COUNT.
 * @return The value in force, or zero for an index out of range.
 */
int32_t testsHelpersParam(test_param_id_t param);

//=============================================================================
// Suite
//=============================================================================

struct sc_command_test_field_s;

/** @brief One test of the suite in tests.c. */
typedef struct {
  ecu_test_id_t id;
  const char *name;    /**< Console name after "run". */
  const char *summary; /**< One line for "list". */
  bool drivesDemand;   /**< Owns the actuator demand while running. */
  bool sequenced;      /**< Part of the "run all" sequence. */
  bool scSupported;    /**< Offered to SerialConfigurator; a test that never
                            ends or leaves a fault behind is not. */
  void (*start)(void); /**< Arms the test; NULL for a pure one-shot. */
  float (*step)(bool *outFinished); /**< Demand per step; NULL finishes at
                                       once, which is how a one-shot ends. */
  const char *(*result)(void); /**< Result token once finished; NULL reports
                                    SC_TEST_RESULT_DONE. */
  size_t (*progress)(struct sc_command_test_field_s *fields,
                     size_t capacity); /**< Progress values while running,
                                            called on the controller core;
                                            NULL reports none. */
} ecu_test_t;

/**
 * @brief The suite defined in tests.c, in sequence order.
 * @param outCount Receives the number of entries.
 * @return First entry; the table lives in flash for the whole run.
 */
const ecu_test_t *testsSuite(size_t *outCount);

#endif /* ECU_FUNCTIONAL_TESTS_ENABLED */

#ifdef __cplusplus
}
#endif

#endif
