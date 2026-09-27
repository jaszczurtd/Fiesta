#ifndef T_TESTS_WORKERS
#define T_TESTS_WORKERS

#include "tests_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file tests_workers.h
 * @brief What the tests of tests.c do: the demand generators and one-shot
 * actions their entries point to, and the progress each one reports.
 *
 * Every generator reads its runtime parameters through testsHelpersParam(),
 * so a change made while a test runs applies from its next step. Nothing here
 * exists without ECU_FUNCTIONAL_TESTS_ENABLED.
 */
#if ECU_FUNCTIONAL_TESTS_ENABLED

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

/** @brief Fixed seed: every run draws the same sequence, so two runs of the
 * random test can be compared directly. */
#define RANDOM_SEED 0x5EEDBEEFU

/** @brief Auto-zero deadline for command S, in milliseconds; zero holds the
 * demand until the next command. Demands at or above
 * VP37_BENCH_HIGH_HOLD_PERCENT use half of it. */
#define VP37_BENCH_HOLD_MS_DEFAULT 0U
#define VP37_BENCH_HOLD_MS_MAX 60000.0f
#define VP37_BENCH_HIGH_HOLD_PERCENT 80.0f

/**
 * @brief Arm every generator and put the command S demand and its auto-zero
 * deadline back to their defaults.
 * @note Leaves the runtime parameters alone; they have their own reset.
 */
void testsWorkersReset(void);

/**
 * @brief Step delay of the cyclic profile in progress.
 * @return Delay in milliseconds; valid while the cyclic test runs.
 */
uint32_t testsWorkersCyclicDelayMs(void);

/**
 * @brief Set the demand the manual test holds, as command S does.
 * @param demand Demand in percent, 0..100.
 * @note Restarts the auto-zero deadline.
 */
void testsWorkersManualSet(float demand);

/**
 * @brief Set the auto-zero deadline of the manual test, as command G does.
 * @param holdMs Deadline in milliseconds; zero holds until the next command.
 */
void testsWorkersManualHoldSet(uint32_t holdMs);

/**
 * @brief Auto-zero deadline of the manual test.
 * @return Deadline in milliseconds; zero means hold.
 */
uint32_t testsWorkersManualHoldMs(void);

/** @brief Arm the cyclic ramp generator at zero. */
void testsWorkersCyclicStart(void);

/**
 * @brief One step of the cyclic ramp.
 * @param outFinished Non-NULL; true once the configured passes are complete.
 * @return Demand in percent.
 */
float testsWorkersCyclicStep(bool *outFinished);

/**
 * @brief Progress of the cyclic test: profile, its rate, cycle and pass.
 * @param fields Non-NULL destination for up to @p capacity values.
 * @param capacity Room in @p fields.
 * @return Values written.
 */
size_t testsWorkersCyclicProgress(struct sc_command_test_field_s *fields,
                                  size_t capacity);

/** @brief Arm the random generator with the fixed seed. */
void testsWorkersRandomStart(void);

/**
 * @brief One step of the random position test.
 * @param outFinished Non-NULL; true once the configured duration elapsed.
 * @return Demand in percent.
 */
float testsWorkersRandomStep(bool *outFinished);

/**
 * @brief Progress of the random test: drawn target, hold and run time left.
 * @param fields Non-NULL destination for up to @p capacity values.
 * @param capacity Room in @p fields.
 * @return Values written.
 */
size_t testsWorkersRandomProgress(struct sc_command_test_field_s *fields,
                                  size_t capacity);

/** @brief Take the demand of command S as the starting point. */
void testsWorkersManualStart(void);

/**
 * @brief One step of the manual hold.
 * @param outFinished Non-NULL; always false, the hold ends on command.
 * @return Demand in percent; zero once an armed auto-zero deadline expires.
 */
float testsWorkersManualStep(bool *outFinished);

/** @brief Arm the upper staircase at its first setpoint. */
void testsWorkersTopStepsStart(void);

/**
 * @brief One step of the upper staircase; each setpoint is held for the
 * top_dwell parameter.
 * @param outFinished Non-NULL; true once top_series series are done.
 * @return Demand in percent.
 */
float testsWorkersTopStepsStep(bool *outFinished);

/**
 * @brief Progress of the upper staircase: setpoint in tenths of a percent
 * and series.
 * @param fields Non-NULL destination for up to @p capacity values.
 * @param capacity Room in @p fields.
 * @return Values written.
 */
size_t testsWorkersTopStepsProgress(struct sc_command_test_field_s *fields,
                                    size_t capacity);

/** @brief Arm the from-rest staircase at its first threshold. */
void testsWorkersTopZeroStart(void);

/**
 * @brief One half-step of the from-rest staircase: a threshold of the same
 * ladder as testsWorkersTopStepsStep(), then a return to zero, each held for
 * the topzero_dwell parameter.
 * @param outFinished Non-NULL; true once topzero_series series are done.
 * @return Demand in percent.
 * @note Every threshold is approached from rest, so its arrival carries
 * nothing over from the previous one.
 */
float testsWorkersTopZeroStep(bool *outFinished);

/**
 * @brief Progress of the from-rest staircase: demand of the half-step in
 * tenths of a percent and series.
 * @param fields Non-NULL destination for up to @p capacity values.
 * @param capacity Room in @p fields.
 * @return Values written.
 */
size_t testsWorkersTopZeroProgress(struct sc_command_test_field_s *fields,
                                   size_t capacity);

/** @brief Arm the pot test at zero demand, first pass. */
void testsWorkersPotStart(void);

/**
 * @brief One step of the pot test: the demand climbs at the pot_rate
 * parameter to 100 %, holds for pot_hold, falls at the same rate and rests
 * for pot_rest.
 * @param outFinished Non-NULL; true once pot_passes passes are done.
 * @return Demand in percent, advanced by the milliseconds since the last call.
 * @note The demand changes on every millisecond of a turn, so the controller
 * sees a tracked target, as it does from the driver's pot.
 */
float testsWorkersPotStep(bool *outFinished);

/**
 * @brief Progress of the pot test: phase, rate and pass.
 * @param fields Non-NULL destination for up to @p capacity values.
 * @param capacity Room in @p fields.
 * @return Values written.
 */
size_t testsWorkersPotProgress(struct sc_command_test_field_s *fields,
                               size_t capacity);

/** @brief Raise one diagnostic trouble code so the storage path can be seen. */
void testsWorkersDtcStart(void);

/** @brief Key the counter of testsWorkersKvStart() lives under. */
#define TESTS_WORKERS_KV_COUNTER_KEY 0xDF00u

/** @brief Outcome of the last testsWorkersKvStart(), for host tests. */
typedef struct {
  uint32_t before;       /**< Counter read before the write, 0 when absent. */
  uint32_t after;        /**< Counter read back after the write. */
  hal_status_t read;     /**< Status of the read before the write. */
  hal_status_t write;    /**< Status of the publication. */
  hal_status_t readBack; /**< Status of the read after the write. */
  bool ok;               /**< Every step succeeded and after == before + 1. */
} tests_workers_kv_result_t;

/** @brief Outcome of the last testsWorkersKvStart(). */
const tests_workers_kv_result_t *testsWorkersKvLastResult(void);

/**
 * @brief Increment a counter in the key-value store and read it back.
 * @note Every write publishes a whole bank through the flash coordinator,
 * with the ADC scan and the other core running as in normal operation, so a
 * refused or broken write shows here as a status, not as a metric drift
 * weeks later. The counter survives resets and power cycles: two runs across
 * one prove persistence.
 */
void testsWorkersKvStart(void);

#endif /* ECU_FUNCTIONAL_TESTS_ENABLED */

#ifdef __cplusplus
}
#endif

#endif
