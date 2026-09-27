/**
 * @file tests_workers.c
 * @brief What every test of tests.c does: the demand generators, the one-shot
 * actions and the progress they report.
 */

#include "tests_workers.h"

#if ECU_FUNCTIONAL_TESTS_ENABLED

#include "dtcManager.h"
#include "ecuPersistence.h"

#include <hal/storage/hal_kv.h>

#include "../common/scDefinitions/sc_command_handlers.h"
#include "../common/scDefinitions/sc_protocol.h"

#include <math.h>

//=============================================================================
// Generator state
//=============================================================================

static const uint32_t s_cyclicDelaysMs[] = {
    CYCLIC_DELAYTIME_A, CYCLIC_DELAYTIME_B, CYCLIC_DELAYTIME_C,
    CYCLIC_DELAYTIME_D};

static struct {
  uint32_t previousMs;
  int32_t value;
  int32_t increment;
  uint32_t profile;
  uint32_t cycles;
  uint32_t passes;
} s_cyclic;

static struct {
  uint32_t state;
  uint32_t startedMs;
  uint32_t holdStartedMs;
  float demand;
} s_random;

static struct {
  uint32_t startedMs;
  uint32_t holdMs;
  float demand;
} s_manual;

/**
 * @brief One series of the upper staircase, in percent of travel.
 *
 * The four setpoints sit in the upper range, where the actuator is least
 * damped and the control loop is most likely to ring. The closing zero drops
 * the actuator back, so every series approaches the top from the same rest
 * position instead of from wherever the previous one ended.
 */
static const float s_topStepsSetpoints[] = {75.0f, 85.5f, 90.5f, 95.0f, 0.0f};

static struct {
  uint32_t stepStartedMs;
  uint32_t series;
  size_t step;
} s_topSteps;

/* Same thresholds, each reached from rest; step counts (threshold, zero)
   pairs, so one index describes both halves. */
static struct {
  uint32_t stepStartedMs;
  uint32_t series;
  size_t step;
} s_topZero;

/* Hand-turned pot: turn up, hold, turn down, rest. */
typedef enum { POT_RISE, POT_HOLD, POT_FALL, POT_REST } pot_phase_t;

static struct {
  pot_phase_t phase;
  uint32_t previousMs;
  uint32_t phaseStartedMs;
  uint32_t passes;
  float demand;
} s_pot;

/** @brief Unsigned view of a parameter the table keeps at one or more. */
static uint32_t paramU32(test_param_id_t param) {
  return (uint32_t)testsHelpersParam(param);
}

//=============================================================================
// Lifecycle
//=============================================================================

/** @brief Put the upper staircase back at the first setpoint of series one. */
static void armTopSteps(void) {
  s_topSteps.stepStartedMs = hal_millis();
  s_topSteps.series = 0U;
  s_topSteps.step = 0U;
}

/** @brief Put the from-rest staircase back at its first threshold. */
static void armTopZero(void) {
  s_topZero.stepStartedMs = hal_millis();
  s_topZero.series = 0U;
  s_topZero.step = 0U;
}

/** @brief Put the pot test at zero demand, first pass, rate untouched. */
static void armPot(void) {
  s_pot.phase = POT_RISE;
  s_pot.previousMs = hal_millis();
  s_pot.phaseStartedMs = s_pot.previousMs;
  s_pot.passes = 0U;
  s_pot.demand = 0.0f;
}

/**
 * @brief Thresholds of the shared ladder, ignoring the entries at rest.
 * @return Count of setpoints above zero.
 * @note The from-rest test supplies its own zero after every threshold, so
 * the closing rest of the shared series would only repeat it.
 */
static size_t topZeroThresholds(void) {
  size_t count = 0U;
  for (size_t i = 0U; i < COUNTOF(s_topStepsSetpoints); i++) {
    if (s_topStepsSetpoints[i] > 0.0f) {
      count++;
    }
  }
  return count;
}

/**
 * @brief Threshold at the given position of the shared ladder.
 * @param index Position among the setpoints above zero.
 * @return Demand in percent, or zero when the position is past the last one.
 */
static float topZeroThresholdAt(size_t index) {
  float demand = 0.0f;
  size_t seen = 0U;
  for (size_t i = 0U; i < COUNTOF(s_topStepsSetpoints); i++) {
    if (s_topStepsSetpoints[i] > 0.0f) {
      if (seen == index) {
        demand = s_topStepsSetpoints[i];
        break;
      }
      seen++;
    }
  }
  return demand;
}

void testsWorkersReset(void) {
  testsWorkersCyclicStart();
  testsWorkersRandomStart();

  s_manual.holdMs = VP37_BENCH_HOLD_MS_DEFAULT;
  s_manual.demand = 0.0f;
  s_manual.startedMs = hal_millis();

  armTopSteps();
  armTopZero();
  armPot();
}

//=============================================================================
// Cyclic ramp
//=============================================================================

void testsWorkersCyclicStart(void) {
  s_cyclic.value = 0;
  s_cyclic.increment = 1;
  s_cyclic.previousMs = hal_millis();
  s_cyclic.profile = 0U;
  s_cyclic.cycles = 0U;
  s_cyclic.passes = 0U;
}

uint32_t testsWorkersCyclicDelayMs(void) {
  return s_cyclicDelaysMs[s_cyclic.profile];
}

float testsWorkersCyclicStep(bool *outFinished) {
  *outFinished = false;
  const uint32_t nowMs = hal_millis();
  if (!hal_elapsed_u32(nowMs, s_cyclic.previousMs,
                       testsWorkersCyclicDelayMs())) {
    return (float)s_cyclic.value;
  }
  s_cyclic.previousMs = nowMs;
  s_cyclic.value += s_cyclic.increment;

  if (s_cyclic.value >= 100) {
    s_cyclic.value = 100;
    s_cyclic.increment = -s_cyclic.increment;
  } else if (s_cyclic.value <= 0) {
    s_cyclic.value = 0;
    s_cyclic.increment = -s_cyclic.increment;
    s_cyclic.cycles++;
    if (s_cyclic.cycles >= paramU32(TEST_PARAM_CYCLIC_CYCLES)) {
      s_cyclic.cycles = 0U;
      s_cyclic.profile++;
      if (s_cyclic.profile >= COUNTOF(s_cyclicDelaysMs)) {
        s_cyclic.profile = 0U;
        s_cyclic.passes++;
        *outFinished = s_cyclic.passes >= paramU32(TEST_PARAM_CYCLIC_PASSES);
      }
    }
  } else {
    // Mid-ramp: nothing to account for.
  }
  return (float)s_cyclic.value;
}

//=============================================================================
// Random positions
//=============================================================================

/** @brief xorshift32: small, deterministic and identical on every target. */
static uint32_t nextRandom(void) {
  uint32_t x = s_random.state;
  x ^= x << 13U;
  x ^= x >> 17U;
  x ^= x << 5U;
  s_random.state = x;
  return x;
}

static void drawRandomDemand(void) {
  s_random.demand = (float)(nextRandom() % 101U);
  s_random.holdStartedMs = hal_millis();
}

void testsWorkersRandomStart(void) {
  s_random.state = RANDOM_SEED;
  s_random.startedMs = hal_millis();
  drawRandomDemand();
}

float testsWorkersRandomStep(bool *outFinished) {
  *outFinished = hal_elapsed_u32(hal_millis(), s_random.startedMs,
                                 paramU32(TEST_PARAM_RANDOM_DURATION) * 1000U);
  if (*outFinished) {
    s_random.demand = 0.0f;
  } else if (hal_elapsed_u32(hal_millis(), s_random.holdStartedMs,
                             paramU32(TEST_PARAM_RANDOM_HOLD) * 1000U)) {
    drawRandomDemand();
  } else {
    // Still inside the hold of the current position.
  }
  return s_random.demand;
}

//=============================================================================
// Manual hold
//=============================================================================

void testsWorkersManualStart(void) { s_manual.startedMs = hal_millis(); }

void testsWorkersManualSet(float demand) {
  s_manual.demand = demand;
  s_manual.startedMs = hal_millis();
}

void testsWorkersManualHoldSet(uint32_t holdMs) { s_manual.holdMs = holdMs; }

uint32_t testsWorkersManualHoldMs(void) { return s_manual.holdMs; }

float testsWorkersManualStep(bool *outFinished) {
  *outFinished = false;
  if ((s_manual.holdMs != 0U) && (s_manual.demand > 0.0f)) {
    const uint32_t deadlineMs = s_manual.demand >= VP37_BENCH_HIGH_HOLD_PERCENT
                                    ? (s_manual.holdMs / 2U)
                                    : s_manual.holdMs;
    if (hal_elapsed_u32(hal_millis(), s_manual.startedMs, deadlineMs)) {
      s_manual.demand = 0.0f;
    }
  }
  return s_manual.demand;
}

//=============================================================================
// Upper travel staircase
//=============================================================================

/** @brief Report the step in progress, so a bench log can be cut into steps. */
static void announceTopStep(void) {
  deb("TEST: top %.1f %% (series %lu/%lu)",
      s_topStepsSetpoints[s_topSteps.step],
      (unsigned long)s_topSteps.series + 1UL,
      (unsigned long)paramU32(TEST_PARAM_TOP_SERIES));
}

void testsWorkersTopStepsStart(void) {
  armTopSteps();
  announceTopStep();
}

float testsWorkersTopStepsStep(bool *outFinished) {
  const uint32_t nowMs = hal_millis();
  *outFinished = false;
  if (hal_elapsed_u32(nowMs, s_topSteps.stepStartedMs,
                      paramU32(TEST_PARAM_TOP_DWELL))) {
    s_topSteps.stepStartedMs = nowMs;
    s_topSteps.step++;
    if (s_topSteps.step >= COUNTOF(s_topStepsSetpoints)) {
      s_topSteps.step = 0U;
      s_topSteps.series++;
      *outFinished = s_topSteps.series >= paramU32(TEST_PARAM_TOP_SERIES);
    }
    if (!*outFinished) {
      announceTopStep();
    }
  } else {
    // Still inside the dwell of the current step.
  }
  return *outFinished ? 0.0f : s_topStepsSetpoints[s_topSteps.step];
}

//=============================================================================
// Upper travel staircase reached from rest
//=============================================================================

/** @brief Demand of the half-step in progress: threshold, then its zero. */
static float topZeroDemand(void) {
  return ((s_topZero.step % 2U) == 0U) ? topZeroThresholdAt(s_topZero.step / 2U)
                                       : 0.0f;
}

/** @brief Report the half-step in progress, so a log can be cut into steps. */
static void announceTopZeroStep(void) {
  deb("TEST: topzero %.1f %% (series %lu/%lu)", topZeroDemand(),
      (unsigned long)s_topZero.series + 1UL,
      (unsigned long)paramU32(TEST_PARAM_TOPZERO_SERIES));
}

void testsWorkersTopZeroStart(void) {
  armTopZero();
  announceTopZeroStep();
}

float testsWorkersTopZeroStep(bool *outFinished) {
  const uint32_t nowMs = hal_millis();
  const size_t steps = topZeroThresholds() * 2U;
  *outFinished = false;
  if (hal_elapsed_u32(nowMs, s_topZero.stepStartedMs,
                      paramU32(TEST_PARAM_TOPZERO_DWELL))) {
    s_topZero.stepStartedMs = nowMs;
    s_topZero.step++;
    if (s_topZero.step >= steps) {
      s_topZero.step = 0U;
      s_topZero.series++;
      *outFinished = s_topZero.series >= paramU32(TEST_PARAM_TOPZERO_SERIES);
    }
    if (!*outFinished) {
      announceTopZeroStep();
    }
  } else {
    // Still inside the dwell of the current half-step.
  }
  return *outFinished ? 0.0f : topZeroDemand();
}

//=============================================================================
// Hand-turned pot
//=============================================================================

/** @brief Report the phase in progress, so a bench log can be cut into it. */
static const char *const s_potPhaseNames[] = {
    SC_TEST_PHASE_RISE, SC_TEST_PHASE_HOLD, SC_TEST_PHASE_FALL,
    SC_TEST_PHASE_REST};

static void announcePotPhase(void) {
  deb("TEST: pot %s %ld %%/s (pass %lu/%lu)", s_potPhaseNames[s_pot.phase],
      (long)testsHelpersParam(TEST_PARAM_POT_RATE),
      (unsigned long)s_pot.passes + 1UL,
      (unsigned long)paramU32(TEST_PARAM_POT_PASSES));
}

static void enterPotPhase(pot_phase_t phase, uint32_t nowMs) {
  s_pot.phase = phase;
  s_pot.phaseStartedMs = nowMs;
  announcePotPhase();
}

void testsWorkersPotStart(void) {
  armPot();
  announcePotPhase();
}

float testsWorkersPotStep(bool *outFinished) {
  const uint32_t nowMs = hal_millis();
  const float step = (float)testsHelpersParam(TEST_PARAM_POT_RATE) * 0.001f *
                     (float)(nowMs - s_pot.previousMs);
  s_pot.previousMs = nowMs;
  *outFinished = false;
  switch (s_pot.phase) {
  case POT_RISE:
    s_pot.demand += step;
    if (s_pot.demand >= 100.0f) {
      s_pot.demand = 100.0f;
      enterPotPhase(POT_HOLD, nowMs);
    }
    break;
  case POT_HOLD:
    if (hal_elapsed_u32(nowMs, s_pot.phaseStartedMs,
                        paramU32(TEST_PARAM_POT_HOLD))) {
      enterPotPhase(POT_FALL, nowMs);
    }
    break;
  case POT_FALL:
    s_pot.demand -= step;
    if (s_pot.demand <= 0.0f) {
      s_pot.demand = 0.0f;
      enterPotPhase(POT_REST, nowMs);
    }
    break;
  default:
    if (hal_elapsed_u32(nowMs, s_pot.phaseStartedMs,
                        paramU32(TEST_PARAM_POT_REST))) {
      s_pot.passes++;
      if (s_pot.passes >= paramU32(TEST_PARAM_POT_PASSES)) {
        *outFinished = true;
      } else {
        enterPotPhase(POT_RISE, nowMs);
      }
    }
    break;
  }
  return s_pot.demand;
}

//=============================================================================
// Progress for the configurator
//=============================================================================

/** @brief Append one numeric progress value when there is room. */
static void progressValue(sc_command_test_field_t *fields, size_t capacity,
                          size_t *count, const char *key, int32_t value) {
  if (*count < capacity) {
    fields[*count] = (sc_command_test_field_t){key, NULL, value};
    (*count)++;
  }
}

/** @brief Milliseconds left of an interval that started at @p startedMs. */
static int32_t remainingMs(uint32_t startedMs, uint32_t intervalMs) {
  const uint32_t elapsed = hal_millis() - startedMs;
  return (elapsed < intervalMs) ? (int32_t)(intervalMs - elapsed) : 0;
}

size_t testsWorkersCyclicProgress(struct sc_command_test_field_s *fields,
                                  size_t capacity) {
  size_t count = 0U;
  // One percent per step: the step delay gives the demand rate.
  progressValue(fields, capacity, &count, SC_TEST_FIELD_PROFILE,
                (int32_t)s_cyclic.profile + 1);
  progressValue(fields, capacity, &count, SC_TEST_FIELD_RATE,
                (int32_t)(1000U / testsWorkersCyclicDelayMs()));
  progressValue(fields, capacity, &count, SC_TEST_FIELD_CYCLE,
                (int32_t)s_cyclic.cycles + 1);
  progressValue(fields, capacity, &count, SC_TEST_FIELD_CYCLES,
                testsHelpersParam(TEST_PARAM_CYCLIC_CYCLES));
  progressValue(fields, capacity, &count, SC_TEST_FIELD_PASS,
                (int32_t)s_cyclic.passes + 1);
  progressValue(fields, capacity, &count, SC_TEST_FIELD_PASSES,
                testsHelpersParam(TEST_PARAM_CYCLIC_PASSES));
  return count;
}

size_t testsWorkersRandomProgress(struct sc_command_test_field_s *fields,
                                  size_t capacity) {
  size_t count = 0U;
  progressValue(fields, capacity, &count, SC_TEST_FIELD_TARGET,
                (int32_t)lroundf(s_random.demand));
  progressValue(fields, capacity, &count, SC_TEST_FIELD_HOLD_LEFT_MS,
                remainingMs(s_random.holdStartedMs,
                            paramU32(TEST_PARAM_RANDOM_HOLD) * 1000U));
  progressValue(fields, capacity, &count, SC_TEST_FIELD_LEFT_S,
                remainingMs(s_random.startedMs,
                            paramU32(TEST_PARAM_RANDOM_DURATION) * 1000U) /
                    1000);
  return count;
}

size_t testsWorkersTopStepsProgress(struct sc_command_test_field_s *fields,
                                    size_t capacity) {
  size_t count = 0U;
  progressValue(fields, capacity, &count, SC_TEST_FIELD_SETPOINT_X10,
                (int32_t)lroundf(s_topStepsSetpoints[s_topSteps.step] * 10.0f));
  progressValue(fields, capacity, &count, SC_TEST_FIELD_SERIES,
                (int32_t)s_topSteps.series + 1);
  progressValue(fields, capacity, &count, SC_TEST_FIELD_SERIES_COUNT,
                testsHelpersParam(TEST_PARAM_TOP_SERIES));
  return count;
}

size_t testsWorkersTopZeroProgress(struct sc_command_test_field_s *fields,
                                   size_t capacity) {
  size_t count = 0U;
  progressValue(fields, capacity, &count, SC_TEST_FIELD_SETPOINT_X10,
                (int32_t)lroundf(topZeroDemand() * 10.0f));
  progressValue(fields, capacity, &count, SC_TEST_FIELD_SERIES,
                (int32_t)s_topZero.series + 1);
  progressValue(fields, capacity, &count, SC_TEST_FIELD_SERIES_COUNT,
                testsHelpersParam(TEST_PARAM_TOPZERO_SERIES));
  return count;
}

size_t testsWorkersPotProgress(struct sc_command_test_field_s *fields,
                               size_t capacity) {
  size_t count = 0U;
  if (count < capacity) {
    fields[count] = (sc_command_test_field_t){SC_TEST_FIELD_PHASE,
                                              s_potPhaseNames[s_pot.phase], 0};
    count++;
  }
  progressValue(fields, capacity, &count, SC_TEST_FIELD_RATE,
                testsHelpersParam(TEST_PARAM_POT_RATE));
  progressValue(fields, capacity, &count, SC_TEST_FIELD_PASS,
                (int32_t)s_pot.passes + 1);
  progressValue(fields, capacity, &count, SC_TEST_FIELD_PASSES,
                testsHelpersParam(TEST_PARAM_POT_PASSES));
  return count;
}

//=============================================================================
// One-shot fixtures
//=============================================================================

void testsWorkersDtcStart(void) {
  const uint16_t code = (uint16_t)DTC_CAN_BUS_FAULT;
  const uint8_t detail = DTC_DETAIL_CAN_TX_RPM;
  dtcManagerSetActiveDetail(code, true, detail);
  deb("TEST: DTC injected: 0x%04X (%s) detail=0x%02X", (unsigned)code,
      dtcManagerGetName(code), (unsigned)detail);
}

/** @brief One deferred batch, the way every ECU storage write is made: the
 * record goes into the RAM image and one commit publishes the bank. */
static hal_status_t kvCounterOperation(const void *user) {
  const uint32_t next = *(const uint32_t *)user;
  hal_status_t status = hal_kv_set_auto_commit(false);
  if (status == HAL_OK) {
    status = hal_kv_set_u32_ex(TESTS_WORKERS_KV_COUNTER_KEY, next);
  }
  const hal_status_t commitStatus = hal_kv_commit_ex();
  const hal_status_t restoreStatus = hal_kv_set_auto_commit(true);
  if (status != HAL_OK) {
    return status;
  }
  return commitStatus != HAL_OK ? commitStatus : restoreStatus;
}

static tests_workers_kv_result_t s_kvResult;

const tests_workers_kv_result_t *testsWorkersKvLastResult(void) {
  return &s_kvResult;
}

void testsWorkersKvStart(void) {
  uint32_t before = 0U;
  hal_status_t readStatus =
      hal_kv_get_u32_ex(TESTS_WORKERS_KV_COUNTER_KEY, &before);
  if (readStatus == HAL_ENOENT) {
    before = 0U;
    readStatus = HAL_OK;
  }
  const uint32_t next = before + 1U;
  const hal_status_t writeStatus =
      ecuPersistenceExecute(kvCounterOperation, &next, NULL);
  uint32_t after = 0U;
  const hal_status_t backStatus =
      hal_kv_get_u32_ex(TESTS_WORKERS_KV_COUNTER_KEY, &after);
  hal_kv_stats_t stats;
  const bool statsOk = hal_kv_get_stats(&stats);
  const bool ok = (readStatus == HAL_OK) && (writeStatus == HAL_OK) &&
                  (backStatus == HAL_OK) && (after == next);
  s_kvResult.before = before;
  s_kvResult.after = after;
  s_kvResult.read = readStatus;
  s_kvResult.write = writeStatus;
  s_kvResult.readBack = backStatus;
  s_kvResult.ok = ok;
  if (ok) {
    deb("TEST: KV counter %lu -> %lu write=%s keys=%u/%u gen=%lu",
        (unsigned long)before, (unsigned long)after,
        hal_status_to_string(writeStatus),
        statsOk ? (unsigned)stats.key_count : 0U,
        statsOk ? (unsigned)stats.key_capacity : 0U,
        statsOk ? (unsigned long)stats.generation : 0UL);
  } else {
    derr("TEST: KV counter %lu -> %lu FAILED read=%s write=%s back=%s "
         "keys=%u/%u",
         (unsigned long)before, (unsigned long)after,
         hal_status_to_string(readStatus), hal_status_to_string(writeStatus),
         hal_status_to_string(backStatus),
         statsOk ? (unsigned)stats.key_count : 0U,
         statsOk ? (unsigned)stats.key_capacity : 0U);
  }
}

#endif /* ECU_FUNCTIONAL_TESTS_ENABLED */
