#ifndef T_TEST_HELPERS
#define T_TEST_HELPERS

#include "tests.h"
#include "vp37.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file test_helpers.h
 * @brief Fixtures of the tests registered in tests.c: demand generators,
 * one-shot actions, their tunable parameters and the console parameter parser.
 *
 * Everything here follows ECU_FUNCTIONAL_TESTS_ENABLED: with tests disabled
 * the translation unit is empty and nothing below is declared.
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

/* Step delays of the four cyclic profiles, in milliseconds. */
#ifndef CYCLIC_DELAYTIME_A
#define CYCLIC_DELAYTIME_A 4U
#endif
#ifndef CYCLIC_DELAYTIME_B
#define CYCLIC_DELAYTIME_B 6U
#endif
#ifndef CYCLIC_DELAYTIME_C
#define CYCLIC_DELAYTIME_C 12U
#endif
#ifndef CYCLIC_DELAYTIME_D
/* 2 ms requests 500 %/s, beyond the normal demand slew limit: stress test. */
#define CYCLIC_DELAYTIME_D 2U
#endif
/** @brief Full 0-100-0 cycles per profile. */
#ifndef CYCLIC_FULL_CYCLES
#define CYCLIC_FULL_CYCLES 6U
#endif
/** @brief Passes over all four profiles before the cyclic test finishes. */
#define CYCLIC_PASSES_DEFAULT 4U
/** @brief Bench ceiling for command Z. */
#define CYCLIC_PASSES_MAX 100.0f

/** @brief Total run time of the random test, in seconds. */
#define RANDOM_DURATION_S_DEFAULT 300U
/** @brief Time one drawn position is held, in seconds. */
#define RANDOM_HOLD_S_DEFAULT 5U
/** @brief Bench ceilings for commands Y and A. */
#define RANDOM_DURATION_S_MAX 3600.0f
#define RANDOM_HOLD_S_MAX 120.0f
/** @brief Fixed seed: every run draws the same sequence, so two runs of the
 * random test can be compared directly. */
#define RANDOM_SEED 0x5EEDBEEFU

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
/** @brief Series the upper staircase repeats before it finishes. */
#ifndef TOP_STEPS_SERIES
#define TOP_STEPS_SERIES 5U
#endif

/** @brief Auto-zero deadline for command S, in milliseconds; zero holds the
 * demand until the next command. Demands at or above
 * VP37_BENCH_HIGH_HOLD_PERCENT use half of it. */
#define VP37_BENCH_HOLD_MS_DEFAULT 0U
#define VP37_BENCH_HOLD_MS_MAX 60000.0f
#define VP37_BENCH_HIGH_HOLD_PERCENT 80.0f

/**
 * @brief Uppercase one ASCII letter.
 * @param value Character to fold.
 * @return Uppercase letter, or the character unchanged.
 */
char testHelpersUpper(char value);

/**
 * @brief Compare two NUL-terminated words without case.
 * @param text Left side, may be NULL.
 * @param word Right side, may be NULL.
 * @return True when both are non-NULL and equal.
 */
bool testHelpersEquals(const char *text, const char *word);

/**
 * @brief Reset every generator and parameter to its built-in default.
 * @return True when the command queue is ready.
 */
bool testHelpersReset(void);

/**
 * @brief Queue one console line for the controller core.
 * @param line NUL-terminated line; must be shorter than VP37_CMD_BUF_SIZE.
 * @return HAL_OK, HAL_EINVAL when the line is empty or too long, or HAL_EBUSY
 * while an earlier line waits to be applied.
 */
hal_status_t testHelpersQueueCommand(const char *line);

/**
 * @brief Take the queued console line, if any.
 * @param out Non-NULL destination of at least VP37_CMD_BUF_SIZE bytes.
 * @return True when a line was copied out and the queue is free again.
 */
bool testHelpersTakeCommand(char *out);

/**
 * @brief Apply one parameter command to the controller.
 * @param self Controller updated under the caller's VP37 mutex.
 * @param cmd NUL-terminated command, first character selects the parameter.
 * @param outStart Non-NULL; receives the test a command asks to start, or
 * START_TEST_NONE.
 * @return True when the command was recognized and applied.
 */
bool testHelpersApplyCommand(VP37Pump *self, const char *cmd,
                             ecu_test_id_t *outStart);

/** @brief Print the parameter commands to the console. */
void testHelpersPrintParameters(void);

/** @brief Arm the cyclic ramp generator at zero. */
void testHelpersCyclicStart(void);

/**
 * @brief One step of the cyclic ramp.
 * @param outFinished Non-NULL; true once the configured passes are complete.
 * @return Demand in percent.
 */
float testHelpersCyclicStep(bool *outFinished);

/** @brief Step delay of the profile in progress, in milliseconds. */
uint32_t testHelpersCyclicDelayMs(void);

/** @brief Arm the random generator with the fixed seed. */
void testHelpersRandomStart(void);

/**
 * @brief One step of the random position test.
 * @param outFinished Non-NULL; true once the configured duration elapsed.
 * @return Demand in percent.
 */
float testHelpersRandomStep(bool *outFinished);

/** @brief Take the demand of command S as the starting point. */
void testHelpersManualStart(void);

/**
 * @brief One step of the manual hold.
 * @param outFinished Non-NULL; always false, the hold ends on command.
 * @return Demand in percent; zero once an armed auto-zero deadline expires.
 */
float testHelpersManualStep(bool *outFinished);

/** @brief Arm the upper staircase at its first setpoint. */
void testHelpersTopStepsStart(void);

/**
 * @brief One step of the upper staircase; each setpoint is held for
 * TOP_STEPS_DWELL_MS.
 * @param outFinished Non-NULL; true once TOP_STEPS_SERIES series are done.
 * @return Demand in percent.
 */
float testHelpersTopStepsStep(bool *outFinished);

/** @brief Arm the from-rest staircase at its first threshold. */
void testHelpersTopZeroStart(void);

/**
 * @brief One half-step of the from-rest staircase: a threshold of the same
 * ladder as testHelpersTopStepsStep(), then a return to zero, each held for
 * TOP_ZERO_DWELL_MS.
 * @param outFinished Non-NULL; true once TOP_STEPS_SERIES series are done.
 * @return Demand in percent.
 * @note Every threshold is approached from rest, so its arrival carries
 * nothing over from the previous one.
 */
float testHelpersTopZeroStep(bool *outFinished);

/** @brief Raise one diagnostic trouble code so the storage path can be seen. */
void testHelpersDtcStart(void);

#endif /* ECU_FUNCTIONAL_TESTS_ENABLED */

#ifdef __cplusplus
}
#endif

#endif
