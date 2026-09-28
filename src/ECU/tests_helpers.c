/**
 * @file tests_helpers.c
 * @brief Everything around the tests: runtime parameters, the console, the
 * runner that starts, sequences and supervises the suite, and the
 * configurator surface.
 */

#include "tests_helpers.h"

#include "../common/scDefinitions/sc_command_handlers.h"

#if ECU_FUNCTIONAL_TESTS_ENABLED

#include "tests_workers.h"

#include <hal/core/hal_compiler.h>
#include <hal/core/hal_mutex_once.h>

#include "../common/scDefinitions/sc_protocol.h"
#include "config.h"
#include "ecuContext.h"
#include "sensors.h"
#include "vp37.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Console command queue
//=============================================================================

static hal_mutex_t s_commandMutex = NULL;
static char s_command[VP37_CMD_BUF_SIZE];
/* Written under the mutex, read without it by the controller core's poll: the
 * loop that releases every control step must not take a lock to learn that
 * nothing waits. */
static uint8_t s_commandLength;

//=============================================================================
// Runtime parameters
//=============================================================================

static const test_param_desc_t s_params[TEST_PARAM_COUNT] = {
    [TEST_PARAM_CYCLIC_PASSES] = {SC_TEST_PARAM_CYCLIC_PASSES,
                                  START_TEST_CYCLIC, SC_TEST_UNIT_COUNT, 1,
                                  CYCLIC_PASSES_MAX, CYCLIC_PASSES_DEFAULT},
    [TEST_PARAM_CYCLIC_CYCLES] = {SC_TEST_PARAM_CYCLIC_CYCLES,
                                  START_TEST_CYCLIC, SC_TEST_UNIT_COUNT, 1,
                                  CYCLIC_FULL_CYCLES_MAX, CYCLIC_FULL_CYCLES},
    [TEST_PARAM_RANDOM_DURATION] = {SC_TEST_PARAM_RANDOM_DURATION,
                                    START_TEST_RANDOM, SC_TEST_UNIT_S, 1,
                                    RANDOM_DURATION_S_MAX,
                                    RANDOM_DURATION_S_DEFAULT},
    [TEST_PARAM_RANDOM_HOLD] = {SC_TEST_PARAM_RANDOM_HOLD, START_TEST_RANDOM,
                                SC_TEST_UNIT_S, 1, RANDOM_HOLD_S_MAX,
                                RANDOM_HOLD_S_DEFAULT},
    [TEST_PARAM_TOP_DWELL] = {SC_TEST_PARAM_TOP_DWELL, START_TEST_TOPSTEPS,
                              SC_TEST_UNIT_MS, TOP_DWELL_MS_MIN,
                              TOP_DWELL_MS_MAX, TOP_STEPS_DWELL_MS},
    [TEST_PARAM_TOP_SERIES] = {SC_TEST_PARAM_TOP_SERIES, START_TEST_TOPSTEPS,
                               SC_TEST_UNIT_COUNT, 1, TOP_SERIES_MAX,
                               TOP_STEPS_SERIES},
    [TEST_PARAM_TOPZERO_DWELL] = {SC_TEST_PARAM_TOPZERO_DWELL,
                                  START_TEST_TOPZERO, SC_TEST_UNIT_MS,
                                  TOP_DWELL_MS_MIN, TOP_DWELL_MS_MAX,
                                  TOP_ZERO_DWELL_MS},
    [TEST_PARAM_TOPZERO_SERIES] = {SC_TEST_PARAM_TOPZERO_SERIES,
                                   START_TEST_TOPZERO, SC_TEST_UNIT_COUNT, 1,
                                   TOP_SERIES_MAX, TOP_STEPS_SERIES},
    [TEST_PARAM_POT_RATE] = {SC_TEST_PARAM_POT_RATE, START_TEST_POT,
                             SC_TEST_UNIT_PCT_PER_S, POT_RATE_PERCENT_PER_S_MIN,
                             POT_RATE_PERCENT_PER_S_MAX,
                             (int32_t)POT_RATE_PERCENT_PER_S_DEFAULT},
    [TEST_PARAM_POT_HOLD] = {SC_TEST_PARAM_POT_HOLD, START_TEST_POT,
                             SC_TEST_UNIT_MS, POT_PHASE_MS_MIN,
                             POT_PHASE_MS_MAX, POT_HOLD_MS},
    [TEST_PARAM_POT_REST] = {SC_TEST_PARAM_POT_REST, START_TEST_POT,
                             SC_TEST_UNIT_MS, POT_PHASE_MS_MIN,
                             POT_PHASE_MS_MAX, POT_REST_MS},
    [TEST_PARAM_POT_PASSES] = {SC_TEST_PARAM_POT_PASSES, START_TEST_POT,
                               SC_TEST_UNIT_COUNT, 1, POT_PASSES_MAX,
                               POT_PASSES},
};

/* Written by either core, read by the tests: one aligned word each. */
static int32_t s_paramValues[TEST_PARAM_COUNT];

const test_param_desc_t *testsHelpersParamDesc(test_param_id_t param) {
  return ((unsigned)param < (unsigned)TEST_PARAM_COUNT) ? &s_params[param]
                                                        : NULL;
}

int32_t testsHelpersParam(test_param_id_t param) {
  return ((unsigned)param < (unsigned)TEST_PARAM_COUNT)
             ? HAL_ATOMIC_LOAD(&s_paramValues[param], HAL_ATOMIC_ACQUIRE)
             : 0;
}

/**
 * @brief Set one runtime parameter; safe from either core.
 * @param param Parameter below TEST_PARAM_COUNT.
 * @param value New value inside the parameter's range.
 * @return HAL_OK, HAL_ENOENT for an index out of range or HAL_EINVAL for a
 * value outside the range, which leaves the old value in force.
 * @note A running test sees the new value the next time it reads it.
 */
static hal_status_t testsHelpersParamSet(test_param_id_t param, int32_t value) {
  const test_param_desc_t *desc = testsHelpersParamDesc(param);
  if (desc == NULL) {
    return HAL_ENOENT;
  }
  if ((value < desc->min) || (value > desc->max)) {
    return HAL_EINVAL;
  }
  HAL_ATOMIC_STORE(&s_paramValues[param], value, HAL_ATOMIC_RELEASE);
  return HAL_OK;
}

/**
 * @brief Find a runtime parameter by its wire id.
 * @param id NUL-terminated id; NULL finds nothing.
 * @param outParam Non-NULL; receives the parameter when found.
 * @return True when @p id names a parameter.
 */
static bool testsHelpersParamFind(const char *id, test_param_id_t *outParam) {
  for (size_t i = 0U; (id != NULL) && (i < COUNTOF(s_params)); i++) {
    if (strcmp(id, s_params[i].id) == 0) {
      *outParam = (test_param_id_t)i;
      return true;
    }
  }
  return false;
}

/** @brief Put every runtime parameter back to its default. */
static void testsHelpersParamsReset(void) {
  for (size_t i = 0U; i < COUNTOF(s_params); i++) {
    HAL_ATOMIC_STORE(&s_paramValues[i], s_params[i].def, HAL_ATOMIC_RELEASE);
  }
}

//=============================================================================
// Lifecycle
//=============================================================================

/**
 * @brief Reset every generator and parameter to its built-in default.
 * @return True when the command queue is ready.
 */
static bool testsHelpersReset(void) {
  HAL_ATOMIC_STORE(&s_commandLength, 0U, HAL_ATOMIC_RELEASE);
  s_command[0] = '\0';

  testsHelpersParamsReset();
  testsWorkersReset();

  return jh_hal_mutex_try_create_once(&s_commandMutex) != NULL;
}

/**
 * @brief Queue one console line for the controller core.
 * @param line NUL-terminated line; must be shorter than VP37_CMD_BUF_SIZE.
 * @return HAL_OK, HAL_EINVAL when the line is empty or too long, or HAL_EBUSY
 * while an earlier line waits to be applied.
 */
static hal_status_t testsHelpersQueueCommand(const char *line) {
  if ((line == NULL) || (line[0] == '\0')) {
    return HAL_EINVAL;
  }
  const size_t length = strlen(line);
  if (length >= COUNTOF(s_command)) {
    return HAL_EINVAL;
  }
  if (jh_hal_mutex_try_create_once(&s_commandMutex) == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_commandMutex);
  const bool busy = s_commandLength != 0U;
  if (!busy) {
    (void)memcpy(s_command, line, length + 1U);
    HAL_ATOMIC_STORE(&s_commandLength, (uint8_t)length, HAL_ATOMIC_RELEASE);
  }
  hal_mutex_unlock(s_commandMutex);
  return busy ? HAL_EBUSY : HAL_OK;
}

/**
 * @brief Take the queued console line, if any.
 * @param out Non-NULL destination of at least VP37_CMD_BUF_SIZE bytes.
 * @return True when a line was copied out and the queue is free again.
 */
static bool testsHelpersTakeCommand(char *out) {
  if ((out == NULL) ||
      (HAL_ATOMIC_LOAD(&s_commandLength, HAL_ATOMIC_ACQUIRE) == 0U) ||
      (jh_hal_mutex_try_create_once(&s_commandMutex) == NULL)) {
    return false;
  }
  hal_mutex_lock(s_commandMutex);
  const uint8_t length = s_commandLength;
  if (length != 0U) {
    (void)memcpy(out, s_command, (size_t)length + 1U);
    HAL_ATOMIC_STORE(&s_commandLength, 0U, HAL_ATOMIC_RELEASE);
  }
  hal_mutex_unlock(s_commandMutex);
  return length != 0U;
}

//=============================================================================
// Parameter commands
//=============================================================================

/** @brief Print the parameter commands to the console. */
static void testsHelpersPrintParameters(void) {
  deb(TEST_HIGHLIGHT_ON
      "Parameters: P<Kp> I<Ki> D<Kd> H<upper Kd 0..0.01> "
      "T<period ms> F<TF s> R(reset) "
      "B(trace) C0/C1(current feedback) L<PWM cap;0=auto> W<thermal weight "
      "0..1> "
      "Q<current observation 0|1> V<supply 0=local|1=period|2=freeze> "
      "M<measured drive compensation 0|1> K<map trim 0|1>" TEST_HIGHLIGHT_OFF);
  deb(TEST_HIGHLIGHT_ON
      "Parameters: N<integral deadband top Hz> U<motion boost up> "
      "J<motion boost down> E<hold confirmation ms> S<demand 0..100> "
      "G<demand auto-zero ms;0=hold> Z<cyclic passes> Y<random seconds> "
      "A<random hold seconds> X1=<pot rate %/s> X2=<top decel %/s2;0=off> "
      "X5=<assist rate cap %/s;0=slew> "
      "X(stop actuator)" TEST_HIGHLIGHT_OFF);
}

/**
 * @brief Uppercase one ASCII letter.
 * @param value Character to fold.
 * @return Uppercase letter, or the character unchanged.
 */
static char testsHelpersUpper(char value) {
  return ((value >= 'a') && (value <= 'z')) ? (char)(value - ('a' - 'A'))
                                            : value;
}

/**
 * @brief Compare two NUL-terminated words without case.
 * @param text Left side, may be NULL.
 * @param word Right side, may be NULL.
 * @return True when both are non-NULL and equal.
 */
static bool testsHelpersEquals(const char *text, const char *word) {
  if ((text == NULL) || (word == NULL)) {
    return false;
  }
  size_t i = 0U;
  while ((text[i] != '\0') && (word[i] != '\0')) {
    if (testsHelpersUpper(text[i]) != testsHelpersUpper(word[i])) {
      return false;
    }
    i++;
  }
  return (text[i] == '\0') && (word[i] == '\0');
}

/** @brief Parse a finite, non-negative number that consumes the whole text. */
static bool parseValue(const char *text, float *outValue) {
  char *end = NULL;
  errno = 0;
  const float value = strtof(text, &end);
  if ((errno != 0) || (end == text) || (*end != '\0') || !isfinite(value) ||
      (value < 0.0f)) {
    return false;
  }
  *outValue = value;
  return true;
}

/** @brief Apply a zero-or-one switch command. */
static bool parseSwitch(const char *cmd, char *outDigit) {
  if ((cmd[1] == '0') || (cmd[1] == '1')) {
    if (cmd[2] == '\0') {
      *outDigit = cmd[1];
      return true;
    }
  }
  return false;
}

static void applyDefaults(VP37Pump *self) {
  VP37_setVP37PID(self, VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, true);
  self->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  self->pid.tf = VP37_PID_TF;
  self->pid.topKd = VP37_PID_TOP_KD;
  self->pid.integralOverride = VP37_BENCH_INTEGRAL_CAP_PWM;
  self->thermal.temperatureCompensationWeight = 1.0f;
  self->currentControl.enabled = true;
  self->pid.integralHoldConfirmMs = VP37_INTEGRAL_HOLD_CONFIRM_MS;
  self->pid.integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  self->feedforward.motionBoostUp = VP37_PWM_FF_MOTION_BOOST_DEFAULT;
  self->feedforward.motionBoostDown = VP37_PWM_FF_DESCENT_BOOST;
  for (uint32_t i = 0U; i < COUNTOF(self->feedforward.mapTrim); i++) {
    self->feedforward.mapTrim[i] = 0.0f;
  }
  self->feedforward.mapTrimTransfers = 0U;
  hal_pid_controller_set_tf(self->pid.controller, self->pid.tf);
  testsHelpersParamsReset();
  testsWorkersManualHoldSet(VP37_BENCH_HOLD_MS_DEFAULT);
  self->demand.topArrivalDecel = VP37_TOP_ARRIVAL_DECEL_PERCENT_PER_S2;
  self->feedforward.motionRateCap = VP37_PWM_FF_MOTION_RATE_CAP_PERCENT_PER_S;
  deb(TEST_HIGHLIGHT_ON
      "PID reset to defaults: Kp=%.4f Ki=%.4f Kd=%.4f TU=%.1f "
      "TF=%.4f" TEST_HIGHLIGHT_OFF,
      VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, VP37_PID_TIME_UPDATE, VP37_PID_TF);
}

/**
 * @brief Apply one numeric parameter.
 * @return True when the value was inside the range for this parameter.
 */
static bool applyValue(VP37Pump *self, char prefix, float value,
                       ecu_test_id_t *outStart) {
  const float upperDerivativeGainMax = 0.01f;
  bool applied = true;
  switch (prefix) {
  case 'P':
    VP37_setVP37PID(self, value, self->pid.ki, self->pid.kd, false);
    deb(TEST_HIGHLIGHT_ON "Kp = %.4f" TEST_HIGHLIGHT_OFF, value);
    break;
  case 'I':
    VP37_setVP37PID(self, self->pid.kp, value, self->pid.kd, false);
    deb(TEST_HIGHLIGHT_ON "Ki = %.4f" TEST_HIGHLIGHT_OFF, value);
    break;
  case 'D':
    VP37_setVP37PID(self, self->pid.kp, self->pid.ki, value, false);
    deb(TEST_HIGHLIGHT_ON "Kd = %.4f" TEST_HIGHLIGHT_OFF, value);
    break;
  case 'H':
    if (value > upperDerivativeGainMax) {
      applied = false;
    } else {
      self->pid.topKd = value;
      deb("VP37 upper derivative gain: %.5f", value);
    }
    break;
  case 'T':
    if ((value < 1.0f) || (value > 100.0f)) {
      return false;
    }
    self->pidTimeUpdate = value;
    deb(TEST_HIGHLIGHT_ON "TU = %.1f" TEST_HIGHLIGHT_OFF, value);
    break;
  case 'F':
    self->pid.tf = value;
    hal_pid_controller_set_tf(self->pid.controller, value);
    deb(TEST_HIGHLIGHT_ON "TF = %.4f" TEST_HIGHLIGHT_OFF, value);
    break;
  case 'L':
    if (value > VP37_BENCH_INTEGRAL_LIMIT_MAX) {
      return false;
    }
    self->pid.integralOverride = value;
    deb("VP37 integral cap: %.1f (0=position profile)", value);
    break;
  case 'W':
    if (value > 1.0f) {
      return false;
    }
    self->thermal.temperatureCompensationWeight = value;
    deb("VP37 thermal weight: %.2f", value);
    break;
  case 'E':
    if ((value > 1000.0f) || (value != floorf(value))) {
      return false;
    }
    self->pid.integralHoldConfirmMs = (uint32_t)value;
    self->pid.integralHoldEnterPending = false;
    deb("VP37 hold confirmation: %lu ms",
        (unsigned long)self->pid.integralHoldConfirmMs);
    break;
  case 'N':
    if (value > VP37_PID_DEADBAND_TOP_MAX_HZ) {
      return false;
    }
    self->pid.integralDeadbandTopHz = value;
    deb("VP37 integral deadband top: %.0f Hz", value);
    break;
  case 'U':
    if (value > VP37_PWM_FF_MOTION_BOOST_MAX) {
      return false;
    }
    self->feedforward.motionBoostUp = value;
    deb("VP37 motion boost up: %.0f", value);
    break;
  case 'J':
    if (value > VP37_PWM_FF_MOTION_BOOST_MAX) {
      return false;
    }
    self->feedforward.motionBoostDown = value;
    deb("VP37 motion boost down: %.0f", value);
    break;
  case 'S':
    if (value > 100.0f) {
      return false;
    }
    testsWorkersManualSet(value);
    *outStart = START_TEST_MANUAL;
    deb("VP37 demand: %.1f auto-zero:%lu ms", value,
        (unsigned long)testsWorkersManualHoldMs());
    break;
  case 'G':
    if (value > VP37_BENCH_HOLD_MS_MAX) {
      return false;
    }
    testsWorkersManualHoldSet((uint32_t)value);
    deb("VP37 demand auto-zero: %lu ms (0=hold)",
        (unsigned long)testsWorkersManualHoldMs());
    break;
  case 'Z':
    if (testsHelpersParamSet(TEST_PARAM_CYCLIC_PASSES, (int32_t)value) !=
        HAL_OK) {
      return false;
    }
    deb("VP37 cyclic passes: %lu",
        (unsigned long)testsHelpersParam(TEST_PARAM_CYCLIC_PASSES));
    break;
  case 'Y':
    if (testsHelpersParamSet(TEST_PARAM_RANDOM_DURATION, (int32_t)value) !=
        HAL_OK) {
      return false;
    }
    deb("VP37 random duration: %lu s",
        (unsigned long)testsHelpersParam(TEST_PARAM_RANDOM_DURATION));
    break;
  case 'A':
    if (testsHelpersParamSet(TEST_PARAM_RANDOM_HOLD, (int32_t)value) !=
        HAL_OK) {
      return false;
    }
    deb("VP37 random hold: %lu s",
        (unsigned long)testsHelpersParam(TEST_PARAM_RANDOM_HOLD));
    break;
  default:
    return false;
  }
  return applied;
}

/**
 * @brief Apply one parameter command to the controller.
 * @param self Controller updated under the caller's VP37 mutex.
 * @param cmd NUL-terminated command, first character selects the parameter.
 * @param outStart Non-NULL; receives the test a command asks to start, or
 * START_TEST_NONE.
 * @return True when the command was recognized and applied.
 */
static bool testsHelpersApplyCommand(VP37Pump *self, const char *cmd,
                                     ecu_test_id_t *outStart) {
  *outStart = START_TEST_NONE;
  const char prefix = testsHelpersUpper(cmd[0]);
  char digit = '\0';

  if ((prefix == 'B') && (cmd[1] == '\0')) {
    const hal_status_t status = VP37_startTrace(self);
    deb("VP37 trace: %s samples:%u", hal_status_to_string(status),
        (unsigned int)VP37_TRACE_SAMPLES);
    return true;
  }
  if ((prefix == 'X') && (cmd[1] == '\0')) {
    VP37_stop(self);
    deb("VP37 stopped; restart ECU to initialize");
    return true;
  }
  if ((prefix == 'X') && (cmd[1] == '1') && (cmd[2] == '=')) {
    float rate = 0.0f;
    if (!parseValue(&cmd[3], &rate) ||
        (testsHelpersParamSet(TEST_PARAM_POT_RATE, (int32_t)lroundf(rate)) !=
         HAL_OK)) {
      return false;
    }
    deb("VP37 pot rate X1: %ld ok",
        (long)testsHelpersParam(TEST_PARAM_POT_RATE));
    return true;
  }
  if ((prefix == 'X') && (cmd[1] == '2') && (cmd[2] == '=')) {
    float decel = 0.0f;
    if (!parseValue(&cmd[3], &decel) || (decel > 20000.0f)) {
      return false;
    }
    self->demand.topArrivalDecel = decel;
    deb("VP37 top arrival decel X2: %.0f ok", decel);
    return true;
  }
  if ((prefix == 'X') && (cmd[1] == '5') && (cmd[2] == '=')) {
    float cap = 0.0f;
    if (!parseValue(&cmd[3], &cap) || (cap > 2000.0f)) {
      return false;
    }
    self->feedforward.motionRateCap = cap;
    deb("VP37 assist rate cap X5: %.0f ok", cap);
    return true;
  }
  if ((prefix == 'R') && (cmd[1] == '\0')) {
    applyDefaults(self);
    return true;
  }
  if ((prefix == 'Q') && parseSwitch(cmd, &digit)) {
    self->thermal.observationEnabled = digit == '1';
    deb("VP37 current observation: %u",
        self->thermal.observationEnabled ? 1U : 0U);
    return true;
  }
  if ((prefix == 'C') && parseSwitch(cmd, &digit)) {
    self->currentControl.enabled = digit == '1';
    deb("VP37 current feedback: %u", self->currentControl.enabled ? 1U : 0U);
    return true;
  }
  if ((prefix == 'K') && parseSwitch(cmd, &digit)) {
    self->feedforward.mapTrimEnabled = digit == '1';
    for (uint32_t i = 0U; i < COUNTOF(self->feedforward.mapTrim); i++) {
      self->feedforward.mapTrim[i] = 0.0f;
    }
    self->feedforward.mapTrimTransfers = 0U;
    deb("VP37 map trim: %u", self->feedforward.mapTrimEnabled ? 1U : 0U);
    return true;
  }
  if ((prefix == 'M') && parseSwitch(cmd, &digit)) {
    self->thermal.driveCompensationEnabled = digit == '1';
    if (!self->thermal.driveCompensationEnabled) {
      self->thermal.driveSamples = 0U;
      self->thermal.driveResistanceReady = false;
      self->thermal.driveCompensationUsed = false;
      self->thermal.driveCorrection = 1.0f;
    }
    deb("VP37 drive compensation: %u",
        self->thermal.driveCompensationEnabled ? 1U : 0U);
    return true;
  }
  if ((prefix == 'V') && (cmd[2] == '\0') &&
      ((cmd[1] == '0') || (cmd[1] == '1') || (cmd[1] == '2'))) {
    // V2 freezes the scale where it is: the supply loop stays open so a
    // supply-side oscillation can be told from one closed through the ECU.
    self->supply.frozen = cmd[1] == '2';
    if (!self->supply.frozen) {
      self->supply.cycleEnabled = cmd[1] == '1';
    }
    deb("VP37 cycle voltage: %u frozen:%u", self->supply.cycleEnabled ? 1U : 0U,
        self->supply.frozen ? 1U : 0U);
    return true;
  }

  float value = 0.0f;
  if ((cmd[1] != '\0') && parseValue(&cmd[1], &value)) {
    return applyValue(self, prefix, value, outStart);
  }
  return false;
}

//=============================================================================
// Runner: starts, sequences and supervises the suite of tests.c
//=============================================================================

/** @brief Who started the running test. */
typedef enum { TESTS_SOURCE_CONSOLE = 0, TESTS_SOURCE_SC } tests_source_t;

static bool s_initialized = false;
static ecu_test_id_t s_active = START_TEST_NONE;
static tests_source_t s_activeSource = TESTS_SOURCE_CONSOLE;
static uint32_t s_activeStartedMs = 0U;
static bool s_sequenceRunning = false;
static bool s_sequenceScOnly = false; /* Sequence of configurator tests. */
static size_t s_sequenceIndex = 0U;
static ecu_test_id_t s_lastTest = START_TEST_NONE;
static const char *s_lastResult = NULL;
static uint32_t s_runs = 0U; /* Tests started since boot. */

static const ecu_test_t *findById(ecu_test_id_t id) {
  size_t total = 0U;
  const ecu_test_t *suite = testsSuite(&total);
  for (size_t i = 0U; i < total; i++) {
    if (suite[i].id == id) {
      return &suite[i];
    }
  }
  return NULL;
}

static const ecu_test_t *findByName(const char *name) {
  size_t total = 0U;
  const ecu_test_t *suite = testsSuite(&total);
  for (size_t i = 0U; i < total; i++) {
    if (testsHelpersEquals(name, suite[i].name)) {
      return &suite[i];
    }
  }
  return NULL;
}

/** @brief Whether an entry belongs to the sequence that is running. */
static bool inSequence(const ecu_test_t *entry, bool scOnly) {
  return entry->sequenced && (!scOnly || entry->scSupported);
}

//=============================================================================
// Configurator requests and status, shared between the cores
//=============================================================================

/** @brief What the configurator asked the controller core to do. */
typedef enum {
  SC_REQUEST_NONE = 0,
  SC_REQUEST_RUN,
  SC_REQUEST_SEQUENCE,
  SC_REQUEST_STOP,
  SC_REQUEST_SKIP
} sc_request_kind_t;

/** @brief Status the controller core publishes for SC_TEST_STATUS. */
typedef struct {
  ecu_test_id_t active;
  tests_source_t source;
  uint32_t runs;
  uint8_t seqIndex;
  uint8_t seqCount;
  uint32_t startedMs;
  bool driveValid;
  int32_t demandX10;
  int32_t positionX10;
  sc_command_test_field_t fields[SC_COMMAND_TEST_FIELDS_MAX];
  size_t fieldCount;
  ecu_test_id_t last;
  const char *result;
} tests_published_t;

static hal_mutex_t s_scMutex = NULL;
/* Written under the mutex, polled without it like the console queue. */
static sc_request_kind_t s_scRequest = SC_REQUEST_NONE;
static ecu_test_id_t s_scRequestTest = START_TEST_NONE;
static tests_published_t s_published;
static uint32_t s_publishedMs = 0U;
/* Written by the session core, read by the controller core. */
static uint32_t s_scContactMs = 0U;
static bool s_scSessionEnded = false;

static bool scLock(void) {
  if (jh_hal_mutex_try_create_once(&s_scMutex) == NULL) {
    return false;
  }
  hal_mutex_lock(s_scMutex);
  return true;
}

static void scTouch(void) {
  HAL_ATOMIC_STORE(&s_scContactMs, hal_millis(), HAL_ATOMIC_RELEASE);
}

static hal_status_t scQueue(sc_request_kind_t kind, ecu_test_id_t test) {
  if (!scLock()) {
    return HAL_ENOMEM;
  }
  const bool busy = s_scRequest != SC_REQUEST_NONE;
  if (!busy) {
    s_scRequestTest = test;
    HAL_ATOMIC_STORE(&s_scRequest, kind, HAL_ATOMIC_RELEASE);
  }
  hal_mutex_unlock(s_scMutex);
  return busy ? HAL_EBUSY : HAL_OK;
}

static sc_request_kind_t scTake(ecu_test_id_t *outTest) {
  if ((HAL_ATOMIC_LOAD(&s_scRequest, HAL_ATOMIC_ACQUIRE) == SC_REQUEST_NONE) ||
      !scLock()) {
    return SC_REQUEST_NONE;
  }
  const sc_request_kind_t kind = s_scRequest;
  *outTest = s_scRequestTest;
  HAL_ATOMIC_STORE(&s_scRequest, SC_REQUEST_NONE, HAL_ATOMIC_RELEASE);
  hal_mutex_unlock(s_scMutex);
  return kind;
}

/** @brief Whether the configurator polled within the keepalive window. */
static bool scPolled(uint32_t *nowMs) {
  /* Read contact first: the other core may publish a newer millisecond. */
  const uint32_t contactMs =
      HAL_ATOMIC_LOAD(&s_scContactMs, HAL_ATOMIC_ACQUIRE);
  *nowMs = hal_millis();
  return !hal_elapsed_u32(*nowMs, contactMs, ECU_SC_TESTS_KEEPALIVE_MS);
}

/** @brief Demand and measured position in percent of the usable stroke. */
static void readDrive(tests_published_t *out) {
  const VP37Pump *pump = &getECUContext()->injectionPump;
  const int32_t bottom = VP37_getPositionDemandMinValue(pump);
  const int32_t top = VP37_getPositionDemandMaxValue(pump);
  out->driveValid = pump->feedback.calibrationDone && (top > bottom) &&
                    (pump->feedback.position >= 0);
  if (out->driveValid) {
    const float span = (float)(top - bottom);
    out->demandX10 =
        (int32_t)lroundf(fmaxf(pump->demand.requestedPercent, 0.0f) * 10.0f);
    out->positionX10 = (int32_t)lroundf(
        (float)(pump->feedback.position - bottom) * 1000.0f / span);
  }
}

/**
 * @brief Publish the status; @p force skips the refresh interval.
 * @note Starts and stops always publish. The periodic refresh of drive and
 * progress runs only while the configurator polls: nobody else reads it, and
 * it shares the loop that releases every control step.
 */
static void publishStatus(bool force) {
  uint32_t nowMs;
  const bool polled = scPolled(&nowMs);
  const uint32_t intervalMs = 20U; /* Refresh while nothing changes. */
  if (!force &&
      (!hal_elapsed_u32(nowMs, s_publishedMs, intervalMs) || !polled)) {
    return;
  }
  s_publishedMs = nowMs;
  tests_published_t next = {0};
  next.active = s_active;
  next.source = s_activeSource;
  next.runs = s_runs;
  next.startedMs = s_activeStartedMs;
  if (s_sequenceRunning && (s_active != START_TEST_NONE)) {
    size_t total = 0U;
    const ecu_test_t *suite = testsSuite(&total);
    for (size_t i = 0U; i < total; i++) {
      if (inSequence(&suite[i], s_sequenceScOnly)) {
        next.seqCount++;
        if (i < s_sequenceIndex) {
          next.seqIndex++;
        }
      }
    }
  }
  readDrive(&next);
  const ecu_test_t *entry = findById(s_active);
  if ((entry != NULL) && (entry->progress != NULL)) {
    next.fieldCount = entry->progress(next.fields, COUNTOF(next.fields));
  }
  next.last = s_lastTest;
  next.result = s_lastResult;
  if (scLock()) {
    s_published = next;
    hal_mutex_unlock(s_scMutex);
  }
}

//=============================================================================
// Activation
//=============================================================================

static void activate(const ecu_test_t *entry, tests_source_t source) {
  s_active = entry->id;
  s_activeSource = source;
  s_activeStartedMs = hal_millis();
  s_runs++;
  // Announce before arming, so whatever the fixture prints reads as its own.
  deb("TEST: %s started", entry->name);
  if (entry->start != NULL) {
    entry->start();
  }
  publishStatus(true);
}

/** @brief Release the running test with the result it ends on, and command
 * zero if it drove the actuator. */
static void deactivate(const char *result) {
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
  s_lastTest = entry->id;
  s_lastResult = result;
  // Bench tools wait for the exact "stopped" line; the result follows it.
  deb("TEST: %s stopped", entry->name);
  deb("TEST: %s result: %s", entry->name, result);
  publishStatus(true);
}

/** @brief Start the next test of the sequence; ends it when none is left. */
static void advanceSequence(tests_source_t source) {
  size_t total = 0U;
  const ecu_test_t *suite = testsSuite(&total);
  while (s_sequenceIndex < total) {
    const ecu_test_t *entry = &suite[s_sequenceIndex];
    s_sequenceIndex++;
    if (inSequence(entry, s_sequenceScOnly)) {
      activate(entry, source);
      return;
    }
  }
  s_sequenceRunning = false;
  deb("TEST: sequence finished");
  publishStatus(true);
}

static void beginSequence(bool scOnly, tests_source_t source) {
  deactivate(SC_TEST_RESULT_STOPPED);
  s_sequenceIndex = 0U;
  s_sequenceRunning = true;
  s_sequenceScOnly = scOnly;
  deb("TEST: sequence started");
  advanceSequence(source);
}

/** @brief Start one test from @p source, ending any running sequence. */
static void beginTest(const ecu_test_t *entry, tests_source_t source) {
  if (s_active != entry->id) {
    deactivate(SC_TEST_RESULT_STOPPED);
  }
  s_sequenceRunning = false;
  activate(entry, source);
}

/** @brief Stop the running test and any sequence, recording why. */
static void stopAll(const char *result) {
  const bool wasActive = s_active != START_TEST_NONE;
  deactivate(result);
  s_sequenceRunning = false;
  if (!wasActive) {
    deb("TEST: none running");
  }
}

/** @brief Stop the running test; a running sequence moves to the next one. */
static void skipOne(void) {
  const bool wasActive = s_active != START_TEST_NONE;
  const tests_source_t source = s_activeSource;
  deactivate(SC_TEST_RESULT_STOPPED);
  if (s_sequenceRunning) {
    advanceSequence(source);
  } else if (!wasActive) {
    deb("TEST: none running");
  } else {
    // The stopped test already reported itself.
  }
}

//=============================================================================
// Public interface
//=============================================================================

bool initTests(void) {
  s_active = START_TEST_NONE;
  s_activeSource = TESTS_SOURCE_CONSOLE;
  s_sequenceRunning = false;
  s_sequenceScOnly = false;
  s_sequenceIndex = 0U;
  s_lastTest = START_TEST_NONE;
  s_lastResult = NULL;
  HAL_ATOMIC_STORE(&s_scRequest, SC_REQUEST_NONE, HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&s_scSessionEnded, false, HAL_ATOMIC_RELEASE);
  /* No configurator is in contact until it calls in. */
  HAL_ATOMIC_STORE(&s_scContactMs, hal_millis() - ECU_SC_TESTS_KEEPALIVE_MS,
                   HAL_ATOMIC_RELEASE);
  s_initialized =
      testsHelpersReset() && (jh_hal_mutex_try_create_once(&s_scMutex) != NULL);
  if (s_initialized) {
    publishStatus(true);
  }
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
    beginSequence(false, TESTS_SOURCE_CONSOLE);
    return HAL_OK;
  }
  const ecu_test_t *entry = findById(test);
  if (entry == NULL) {
    return HAL_EINVAL;
  }
  beginTest(entry, TESTS_SOURCE_CONSOLE);
  return HAL_OK;
}

hal_status_t stopTest(void) {
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  skipOne();
  return HAL_OK;
}

hal_status_t stopTests(void) {
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  stopAll(SC_TEST_RESULT_STOPPED);
  return HAL_OK;
}

const char *testsActiveName(void) {
  const ecu_test_t *entry = findById(s_active);
  return entry != NULL ? entry->name : NULL;
}

uint32_t testsCyclicDelayMs(void) {
  return s_active == START_TEST_CYCLIC ? testsWorkersCyclicDelayMs() : 0U;
}

void testsScSessionEnded(void) {
  HAL_ATOMIC_STORE(&s_scSessionEnded, true, HAL_ATOMIC_RELEASE);
}

//=============================================================================
// Console
//=============================================================================

static void printHelp(void) {
  deb(TEST_HIGHLIGHT_ON "Tests: run <name> | run all | stop | skip | list | "
                        "params | set <param> <value> | ?" TEST_HIGHLIGHT_OFF);
  testsHelpersPrintParameters();
}

static void printList(void) {
  const char *active = testsActiveName();
  size_t total = 0U;
  const ecu_test_t *suite = testsSuite(&total);
  for (size_t i = 0U; i < total; i++) {
    deb("TEST: %-7s %-42s %s%s%s", suite[i].name, suite[i].summary,
        suite[i].sequenced ? "in sequence" : "on request",
        suite[i].scSupported ? ", configurator" : "",
        testsHelpersEquals(active, suite[i].name) ? ", running" : "");
  }
}

static void printParams(void) {
  for (size_t i = 0U; i < (size_t)TEST_PARAM_COUNT; i++) {
    const test_param_desc_t *desc = testsHelpersParamDesc((test_param_id_t)i);
    deb("TEST: %-16s %ld %s [%ld..%ld, default %ld]", desc->id,
        (long)testsHelpersParam((test_param_id_t)i), desc->unit,
        (long)desc->min, (long)desc->max, (long)desc->def);
  }
}

/** @brief Apply "set <param> <value>" to one runtime parameter. */
static void dispatchSet(const char *args) {
  char id[SC_TEST_ID_MAX] = {0};
  size_t length = 0U;
  while ((args[length] != '\0') && (args[length] != ' ') &&
         (length + 1U < sizeof(id))) {
    id[length] = args[length];
    length++;
  }
  test_param_id_t param = TEST_PARAM_COUNT;
  char *end = NULL;
  const long value = strtol(&args[length], &end, 10);
  if (!testsHelpersParamFind(id, &param) || (end == &args[length]) ||
      (*end != '\0') ||
      (testsHelpersParamSet(param, (int32_t)value) != HAL_OK)) {
    derr("TEST: set %s rejected; send params for ids and ranges", id);
    return;
  }
  deb("TEST: set %s = %ld ok", id, (long)testsHelpersParam(param));
  publishStatus(true);
}

/** @brief Split "run <name>" and start the named test. */
static void dispatchRun(const char *name) {
  if (testsHelpersEquals(name, "all")) {
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

/** @brief Length of @p word when @p cmd starts with it, case folded; else 0. */
static size_t commandPrefix(const char *cmd, const char *word) {
  size_t i = 0U;
  while (word[i] != '\0') {
    if (testsHelpersUpper(cmd[i]) != testsHelpersUpper(word[i])) {
      return 0U;
    }
    i++;
  }
  return i;
}

static void dispatch(const char *line) {
  size_t skip = 0U;
  while (line[skip] == ' ') {
    skip++;
  }
  const char *cmd = &line[skip];
  if (cmd[0] == '\0') {
    return;
  }

  if (((cmd[0] == '?') && (cmd[1] == '\0')) ||
      testsHelpersEquals(cmd, "help")) {
    printHelp();
    return;
  }
  if (testsHelpersEquals(cmd, "list")) {
    printList();
    return;
  }
  if (testsHelpersEquals(cmd, "params")) {
    printParams();
    return;
  }
  if (testsHelpersEquals(cmd, "stop")) {
    (void)stopTests();
    return;
  }
  if (testsHelpersEquals(cmd, "skip")) {
    (void)stopTest();
    return;
  }
  size_t prefix = commandPrefix(cmd, "run ");
  if (prefix != 0U) {
    dispatchRun(&cmd[prefix]);
    return;
  }
  prefix = commandPrefix(cmd, "set ");
  if (prefix != 0U) {
    dispatchSet(&cmd[prefix]);
    return;
  }

  ecu_test_id_t requested = START_TEST_NONE;
  VP37Pump *pump = &getECUContext()->injectionPump;
  if (!testsHelpersApplyCommand(pump, cmd, &requested)) {
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
  const hal_status_t status = testsHelpersQueueCommand(line);
  if (status == HAL_EBUSY) {
    derr("Test command pending; retry after acknowledgement");
  } else if (status == HAL_EINVAL) {
    derr("Test command rejected: empty or too long");
  } else {
    // Queued for the controller core.
  }
}

//=============================================================================
// Controller core
//=============================================================================

/** @brief Apply the request the configurator queued, if any. */
static void applyScRequest(void) {
  ecu_test_id_t test = START_TEST_NONE;
  switch (scTake(&test)) {
  case SC_REQUEST_RUN: {
    const ecu_test_t *entry = findById(test);
    if (entry != NULL) {
      beginTest(entry, TESTS_SOURCE_SC);
    }
    break;
  }
  case SC_REQUEST_SEQUENCE:
    beginSequence(true, TESTS_SOURCE_SC);
    break;
  case SC_REQUEST_STOP:
    stopAll(SC_TEST_RESULT_STOPPED);
    break;
  case SC_REQUEST_SKIP:
    skipOne();
    break;
  default:
    break;
  }
}

/**
 * @brief Stop a configurator test nobody supervises any more.
 * @note The session ended, the host went quiet for ECU_SC_TESTS_KEEPALIVE_MS,
 * or, with the interlock on, the engine started. Console tests are the
 * bench operator's and are left alone, and so is a session end seen while no
 * configurator test runs: scRun() clears it before queuing the next one.
 */
static void superviseScTest(void) {
  if ((s_active == START_TEST_NONE) || (s_activeSource != TESTS_SOURCE_SC)) {
    return;
  }
  const bool ended =
      HAL_ATOMIC_LOAD(&s_scSessionEnded, HAL_ATOMIC_ACQUIRE) &&
      HAL_ATOMIC_EXCHANGE(&s_scSessionEnded, false, HAL_ATOMIC_ACQ_REL);
  uint32_t nowMs;
  const char *reason = NULL;
  if (ended) {
    reason = SC_TEST_RESULT_SESSION_END;
  } else if (!scPolled(&nowMs)) {
    reason = SC_TEST_RESULT_HOST_LOST;
#if ECU_SC_TESTS_RPM_INTERLOCK
  } else if (getGlobalValue(F_RPM) > (float)ECU_SC_TESTS_RPM_LIMIT) {
    reason = SC_TEST_RESULT_ENGINE_RUNNING;
#endif
  } else {
    // Supervised and allowed to run.
  }
  if (reason != NULL) {
    derr("TEST: configurator test stopped: %s", reason);
    stopAll(reason);
  }
}

bool tickTests(void) {
  if (!s_initialized) {
    return false;
  }

  applyScRequest();
  char line[VP37_CMD_BUF_SIZE];
  if (testsHelpersTakeCommand(line)) {
    dispatch(line);
  }
  superviseScTest();

  const ecu_test_t *entry = findById(s_active);
  if (entry == NULL) {
    publishStatus(false);
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
    const tests_source_t source = s_activeSource;
    deactivate((entry->result != NULL) ? entry->result() : SC_TEST_RESULT_DONE);
    if (s_sequenceRunning) {
      advanceSequence(source);
    }
  }
  publishStatus(false);
  return owned;
}

//=============================================================================
// Configurator operations, on the core that polls the serial session
//=============================================================================

/** @brief Registry entry of the n-th configurator test. */
static const ecu_test_t *scTestAt(size_t index) {
  size_t total = 0U;
  const ecu_test_t *suite = testsSuite(&total);
  size_t seen = 0U;
  for (size_t i = 0U; i < total; i++) {
    if (suite[i].scSupported) {
      if (seen == index) {
        return &suite[i];
      }
      seen++;
    }
  }
  return NULL;
}

static size_t scParamCount(ecu_test_id_t test) {
  size_t count = 0U;
  for (size_t i = 0U; i < (size_t)TEST_PARAM_COUNT; i++) {
    if (testsHelpersParamDesc((test_param_id_t)i)->test == test) {
      count++;
    }
  }
  return count;
}

static void scFillParam(test_param_id_t param, sc_command_test_param_t *out) {
  const test_param_desc_t *desc = testsHelpersParamDesc(param);
  const ecu_test_t *owner = findById(desc->test);
  *out =
      (sc_command_test_param_t){desc->id,   (owner != NULL) ? owner->name : "",
                                desc->unit, testsHelpersParam(param),
                                desc->min,  desc->max,
                                desc->def};
}

static size_t scCount(void *user) {
  (void)user;
  size_t count = 0U;
  while (scTestAt(count) != NULL) {
    count++;
  }
  return count;
}

static bool scInfo(void *user, size_t index, sc_command_test_info_t *out) {
  (void)user;
  const ecu_test_t *entry = scTestAt(index);
  if (entry == NULL) {
    return false;
  }
  *out = (sc_command_test_info_t){entry->name, inSequence(entry, true),
                                  scParamCount(entry->id)};
  return true;
}

static bool scParamAt(void *user, size_t testIndex, size_t paramIndex,
                      sc_command_test_param_t *out) {
  (void)user;
  const ecu_test_t *entry = scTestAt(testIndex);
  size_t seen = 0U;
  for (size_t i = 0U; (entry != NULL) && (i < (size_t)TEST_PARAM_COUNT); i++) {
    if (testsHelpersParamDesc((test_param_id_t)i)->test == entry->id) {
      if (seen == paramIndex) {
        scFillParam((test_param_id_t)i, out);
        return true;
      }
      seen++;
    }
  }
  return false;
}

static bool scParam(void *user, const char *id, sc_command_test_param_t *out) {
  (void)user;
  test_param_id_t param = TEST_PARAM_COUNT;
  if (!testsHelpersParamFind(id, &param)) {
    return false;
  }
  scFillParam(param, out);
  return true;
}

static hal_status_t scSetParam(void *user, const char *id, int32_t value) {
  (void)user;
  scTouch();
  test_param_id_t param = TEST_PARAM_COUNT;
  return testsHelpersParamFind(id, &param) ? testsHelpersParamSet(param, value)
                                           : HAL_ENOENT;
}

static hal_status_t scRun(void *user, const char *name) {
  (void)user;
  if (!s_initialized) {
    return HAL_EUNINIT;
  }
  scTouch();
  const bool sequence = strcmp(name, SC_TEST_SEQUENCE) == 0;
  const ecu_test_t *entry = sequence ? NULL : findByName(name);
  if (!sequence && ((entry == NULL) || !entry->scSupported)) {
    return HAL_ENOENT;
  }
#if ECU_SC_TESTS_RPM_INTERLOCK
  if (getGlobalValue(F_RPM) > (float)ECU_SC_TESTS_RPM_LIMIT) {
    return HAL_EPERM;
  }
#endif
  HAL_ATOMIC_STORE(&s_scSessionEnded, false, HAL_ATOMIC_RELEASE);
  return sequence ? scQueue(SC_REQUEST_SEQUENCE, START_TEST_NONE)
                  : scQueue(SC_REQUEST_RUN, entry->id);
}

static hal_status_t scStop(void *user) {
  (void)user;
  scTouch();
  return scQueue(SC_REQUEST_STOP, START_TEST_NONE);
}

static hal_status_t scSkip(void *user) {
  (void)user;
  scTouch();
  return scQueue(SC_REQUEST_SKIP, START_TEST_NONE);
}

static void scStatus(void *user, sc_command_test_status_t *out) {
  (void)user;
  scTouch();
  tests_published_t snapshot = {0};
  if (scLock()) {
    snapshot = s_published;
    hal_mutex_unlock(s_scMutex);
  }
  const ecu_test_t *active = findById(snapshot.active);
  const ecu_test_t *last = findById(snapshot.last);
  *out = (sc_command_test_status_t){0};
  out->active = (active != NULL) ? active->name : NULL;
  out->source = (snapshot.source == TESTS_SOURCE_SC) ? SC_TEST_SOURCE_SC
                                                     : SC_TEST_SOURCE_CONSOLE;
  out->runs = snapshot.runs;
  out->seq_index = snapshot.seqIndex;
  out->seq_count = snapshot.seqCount;
  out->elapsed_ms = (active != NULL) ? (hal_millis() - snapshot.startedMs) : 0U;
  out->drive_valid = snapshot.driveValid;
  out->demand_x10 = snapshot.demandX10;
  out->position_x10 = snapshot.positionX10;
  out->field_count = snapshot.fieldCount;
  (void)memcpy(out->fields, snapshot.fields, sizeof(out->fields));
  out->last = (last != NULL) ? last->name : NULL;
  out->result = snapshot.result;
}

const struct sc_command_test_ops_s *testsScOps(void) {
  static const sc_command_test_ops_t s_scOps = {
      scCount, scInfo, scParamAt, scParam,  scSetParam,
      scRun,   scStop, scSkip,    scStatus,
  };
  return &s_scOps;
}

#else /* ECU_FUNCTIONAL_TESTS_ENABLED */

//=============================================================================
// Tests are not compiled in: the ECU keeps its normal demand source, no
// command can reach a fixture and the configurator sees an empty catalog.
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

void testsScSessionEnded(void) {}

static size_t scCount(void *user) {
  (void)user;
  return 0U;
}

static bool scInfo(void *user, size_t index, sc_command_test_info_t *out) {
  (void)user;
  (void)index;
  (void)out;
  return false;
}

static bool scParamAt(void *user, size_t testIndex, size_t paramIndex,
                      sc_command_test_param_t *out) {
  (void)user;
  (void)testIndex;
  (void)paramIndex;
  (void)out;
  return false;
}

static bool scParam(void *user, const char *id, sc_command_test_param_t *out) {
  (void)user;
  (void)id;
  (void)out;
  return false;
}

static hal_status_t scSetParam(void *user, const char *id, int32_t value) {
  (void)user;
  (void)id;
  (void)value;
  return HAL_ENOENT;
}

static hal_status_t scRun(void *user, const char *name) {
  (void)user;
  (void)name;
  return HAL_ENOENT;
}

static hal_status_t scNothingToDo(void *user) {
  (void)user;
  return HAL_OK;
}

static void scStatus(void *user, sc_command_test_status_t *out) {
  (void)user;
  *out = (sc_command_test_status_t){0};
}

const struct sc_command_test_ops_s *testsScOps(void) {
  static const sc_command_test_ops_t s_scOps = {
      scCount, scInfo,        scParamAt,     scParam,  scSetParam,
      scRun,   scNothingToDo, scNothingToDo, scStatus,
  };
  return &s_scOps;
}

#endif /* ECU_FUNCTIONAL_TESTS_ENABLED */
