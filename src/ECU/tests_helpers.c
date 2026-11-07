/**
 * @file tests_helpers.c
 * @brief ECU parameters and VP37 policy for the shared functional-test runner.
 */

#include "tests_helpers.h"

#include "../common/scDefinitions/sc_command_handlers.h"

#if ECU_FUNCTIONAL_TESTS_ENABLED

#include "tests_workers.h"

#include <hal/core/hal_compiler.h>

#include "../common/scDefinitions/sc_protocol.h"
#include "../common/vp37/vp37_current.h"
#include "../common/vp37/vp37_internal.h"
#include "../common/vp37/vp37_tuning.h"
#include "config.h"
#include "ecuContext.h"
#include "sensors.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
/* Pending replies: controller adds, session consumes one per event poll. */
static uint32_t s_defaultsReplies;
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
  return fiesta_test_param((size_t)param);
}
//=============================================================================
// Parameter commands
//=============================================================================

/** @brief Print the parameter commands to the console. */
static void testsHelpersPrintParameters(void) {
  deb(TEST_HIGHLIGHT_ON
      "Parameters: P<Kp> I<Ki> D<Kd> H<upper Kd 0..0.01> "
      "T<period ms> F<TF s> R(reset) "
      "B(trace) B2(stream) B0(end stream) C0/C1(current feedback) "
      "L<PWM cap;0=auto> W<thermal weight "
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
  VP37_resetTuning(self);
  testsWorkersSetFixedCyclicPeriod(0U);
  fiesta_test_params_reset();
  testsWorkersManualHoldSet(VP37_BENCH_HOLD_MS_DEFAULT);
  (void)HAL_ATOMIC_FETCH_ADD(&s_defaultsReplies, 1U, HAL_ATOMIC_RELEASE);
}

/** @brief Set one pump parameter; true when the value was inside its range. */
static bool tune(VP37Pump *self, VP37TuningParam param, float value) {
  return VP37_setTuning(self, param, value) == HAL_OK;
}

/**
 * @brief Apply one numeric parameter.
 * @return True when the value was inside the range for this parameter.
 */
static bool applyValue(VP37Pump *self, char prefix, float value,
                       ecu_test_id_t *outStart) {
  bool applied = false;
  switch (prefix) {
  case 'P':
    applied = tune(self, VP37_TUNING_KP, value);
    if (applied) {
      deb(TEST_HIGHLIGHT_ON "Kp = %.4f" TEST_HIGHLIGHT_OFF, value);
    }
    break;
  case 'I':
    applied = tune(self, VP37_TUNING_KI, value);
    if (applied) {
      deb(TEST_HIGHLIGHT_ON "Ki = %.4f" TEST_HIGHLIGHT_OFF, value);
    }
    break;
  case 'D':
    applied = tune(self, VP37_TUNING_KD, value);
    if (applied) {
      deb(TEST_HIGHLIGHT_ON "Kd = %.4f" TEST_HIGHLIGHT_OFF, value);
    }
    break;
  case 'H':
    applied = tune(self, VP37_TUNING_TOP_KD, value);
    if (applied) {
      deb("VP37 upper derivative gain: %.5f", value);
    }
    break;
  case 'T':
    applied = tune(self, VP37_TUNING_PERIOD_MS, value);
    if (applied) {
      deb(TEST_HIGHLIGHT_ON "TU = %.1f" TEST_HIGHLIGHT_OFF, value);
    }
    break;
  case 'F':
    applied = tune(self, VP37_TUNING_DERIVATIVE_FILTER, value);
    if (applied) {
      deb(TEST_HIGHLIGHT_ON "TF = %.4f" TEST_HIGHLIGHT_OFF, value);
    }
    break;
  case 'L':
    applied = tune(self, VP37_TUNING_INTEGRAL_CAP, value);
    if (applied) {
      deb("VP37 integral cap: %.1f (0=position profile)", value);
    }
    break;
  case 'W':
    applied = tune(self, VP37_TUNING_TEMPERATURE_WEIGHT, value);
    if (applied) {
      deb("VP37 thermal weight: %.2f", value);
    }
    break;
  case 'E':
    applied = tune(self, VP37_TUNING_HOLD_CONFIRM_MS, value);
    if (applied) {
      deb("VP37 hold confirmation: %lu ms", (unsigned long)value);
    }
    break;
  case 'N':
    applied = tune(self, VP37_TUNING_DEADBAND_TOP_HZ, value);
    if (applied) {
      deb("VP37 integral deadband top: %.0f Hz", value);
    }
    break;
  case 'U':
    applied = tune(self, VP37_TUNING_MOTION_BOOST_UP, value);
    if (applied) {
      deb("VP37 motion boost up: %.0f", value);
    }
    break;
  case 'J':
    applied = tune(self, VP37_TUNING_MOTION_BOOST_DOWN, value);
    if (applied) {
      deb("VP37 motion boost down: %.0f", value);
    }
    break;
  case 'S':
    if (value <= 100.0f) {
      testsWorkersManualSet(value);
      *outStart = START_TEST_MANUAL;
      deb("VP37 demand: %.1f auto-zero:%lu ms", value,
          (unsigned long)testsWorkersManualHoldMs());
      applied = true;
    }
    break;
  case 'G':
    if (value <= VP37_BENCH_HOLD_MS_MAX) {
      testsWorkersManualHoldSet((uint32_t)value);
      deb("VP37 demand auto-zero: %lu ms (0=hold)",
          (unsigned long)testsWorkersManualHoldMs());
      applied = true;
    }
    break;
  case 'Z':
    applied = fiesta_test_param_set(TEST_PARAM_CYCLIC_PASSES, (int32_t)value) ==
              HAL_OK;
    if (applied) {
      deb("VP37 cyclic passes: %lu",
          (unsigned long)testsHelpersParam(TEST_PARAM_CYCLIC_PASSES));
    }
    break;
  case 'Y':
    applied = fiesta_test_param_set(TEST_PARAM_RANDOM_DURATION,
                                    (int32_t)value) == HAL_OK;
    if (applied) {
      deb("VP37 random duration: %lu s",
          (unsigned long)testsHelpersParam(TEST_PARAM_RANDOM_DURATION));
    }
    break;
  case 'A':
    applied =
        fiesta_test_param_set(TEST_PARAM_RANDOM_HOLD, (int32_t)value) == HAL_OK;
    if (applied) {
      deb("VP37 random hold: %lu s",
          (unsigned long)testsHelpersParam(TEST_PARAM_RANDOM_HOLD));
    }
    break;
  default:
    break;
  }
  return applied;
}

/** @brief Zero-or-one console switches and the pump setting each one drives. */
static const struct {
  char prefix;
  VP37TuningParam param;
  const char *name;
} k_testsHelpersSwitches[] = {
    {'Q', VP37_TUNING_CURRENT_OBSERVATION, "current observation"},
    {'C', VP37_TUNING_CURRENT_FEEDBACK, "current feedback"},
    {'K', VP37_TUNING_MAP_TRIM, "map trim"},
    {'M', VP37_TUNING_DRIVE_COMPENSATION, "drive compensation"},
};

/** @brief Whether a tuning switch reads on. */
static unsigned int switchOn(const VP37Pump *self, VP37TuningParam param) {
  return (VP37_getTuning(self, param) > 0.5f) ? 1U : 0U;
}

/**
 * @brief Apply one parameter command to the controller.
 * @param self Controller owned by the calling control core.
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

  if ((prefix == 'B') && ((cmd[1] == '0') || (cmd[1] == '2')) &&
      (cmd[2] == '\0')) {
    const hal_status_t status = (cmd[1] == '2') ? VP37_startMetricStream(self)
                                                : VP37_stopMetricStream();
    deb("VP37 stream: %s", hal_status_to_string(status));
    return true;
  }

  const bool descentTrace =
      (prefix == 'B') && (cmd[1] == '1') && (cmd[2] == '\0');
  if ((prefix == 'B') && ((cmd[1] == '\0') || descentTrace)) {
    const hal_status_t status =
        descentTrace ? VP37_startTraceOnDescent(self) : VP37_startTrace(self);
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
        (fiesta_test_param_set(TEST_PARAM_POT_RATE, (int32_t)lroundf(rate)) !=
         HAL_OK)) {
      return false;
    }
    deb("VP37 pot rate X1: %ld ok",
        (long)testsHelpersParam(TEST_PARAM_POT_RATE));
    return true;
  }
  if ((prefix == 'X') && (cmd[1] == '2') && (cmd[2] == '=')) {
    float decel = 0.0f;
    if (!parseValue(&cmd[3], &decel) ||
        !tune(self, VP37_TUNING_TOP_ARRIVAL_DECEL, decel)) {
      return false;
    }
    deb("VP37 top arrival decel X2: %.0f ok", decel);
    return true;
  }
  if ((prefix == 'X') && (cmd[1] == '5') && (cmd[2] == '=')) {
    float cap = 0.0f;
    if (!parseValue(&cmd[3], &cap) ||
        !tune(self, VP37_TUNING_MOTION_RATE_CAP, cap)) {
      return false;
    }
    deb("VP37 assist rate cap X5: %.0f ok", cap);
    return true;
  }
  if ((prefix == 'X') && (cmd[1] == '6') && (cmd[2] == '=')) {
    float offsetMs = 0.0f;
    if (!parseValue(&cmd[3], &offsetMs) ||
        !tune(self, VP37_TUNING_HOLD_OFFSET_MS, offsetMs)) {
      return false;
    }
    deb("VP37 hold offset release X6: %.0f ms ok", offsetMs);
    return true;
  }
  if ((prefix == 'X') && (cmd[1] == '7') && (cmd[2] == '=')) {
    float mode = 0.0f;
    if (!parseValue(&cmd[3], &mode) ||
        !tune(self, VP37_TUNING_THERMAL_HOLD, mode)) {
      return false;
    }
    deb("VP37 thermal hold X7: %.0f ok", mode);
    return true;
  }
  if ((prefix == 'X') && (cmd[1] == '8') && (cmd[2] == '=')) {
    float band = 0.0f;
    if (!parseValue(&cmd[3], &band) ||
        !tune(self, VP37_TUNING_HOLD_MEAN_ENTER_HZ, band)) {
      return false;
    }
    deb("VP37 hold mean entry X8: %.1f Hz ok", band);
    return true;
  }
  if ((prefix == 'X') && (cmd[1] == '9') && (cmd[2] == '=')) {
    float on = 0.0f;
    if (!parseValue(&cmd[3], &on) ||
        !tune(self, VP37_TUNING_HOLD_CURRENT, on)) {
      return false;
    }
    deb("VP37 hold current X9: %.0f ok", on);
    return true;
  }
  if ((strncmp(cmd, "X15=", 4U) == 0) || (strncmp(cmd, "X16=", 4U) == 0)) {
    const bool referenceCommand = strncmp(cmd, "X15=", 4U) == 0;
    char enabled = '\0';
    if (!parseSwitch(&cmd[3], &enabled) || !self->demand.atRest ||
        self->demand.requestedPercent != 0.0f || self->output.finalPWM != 0 ||
        self->thermal.driveCompensationEnabled ||
        self->currentControl.enabled || self->supply.cycleEnabled) {
      return false;
    }
    VP37_resetTuning(self);
    self->thermal.driveCompensationEnabled = false;
    self->currentControl.enabled = false;
    self->supply.cycleEnabled = false;
    self->pid.septemberReference = referenceCommand && (enabled == '1');
    self->pid.fullRangePrototype = !referenceCommand && (enabled == '1');
    if (self->pid.septemberReference) {
      VP37_setVP37PID(self, 0.05f, 0.2f, 0.001f, true);
      self->pid.topKd = 0.0f;
      self->thermal.scaleHoldEnabled = false;
      self->feedforward.motionBoostUp = 32.0f;
      self->feedforward.motionBoostDown = 0.0f;
      self->feedforward.motionRateCap = 0.0f;
      self->demand.topArrivalDecel = 0.0f;
    }
    if (self->pid.fullRangePrototype) {
      self->pid.integralDeadbandTopHz = 0.0f;
      self->thermal.scaleHoldEnabled = false;
    }
    deb("VP37 research %s X%u: %c ok",
        referenceCommand ? "reference" : "prototype",
        referenceCommand ? 15U : 16U, enabled);
    return true;
  }
  if (strcmp(cmd, "X19=1") == 0) {
    if (testsActiveName() != NULL) {
      return false;
    }
    int32_t maximum = 0;
    const hal_status_t status = VP37_probeCalibrationMaximum(self, &maximum);
    deb("VP37 research calibration probe X19: status:%d measured:%ld done",
        status, (long)maximum);
    return true;
  }
  if (strncmp(cmd, "X23=", 4U) == 0) {
    float enabled = 0.0f;
    if (!parseValue(&cmd[4], &enabled) ||
        !tune(self, VP37_TUNING_RESEARCH_UPPER_DEADBAND, enabled)) {
      return false;
    }
    deb("VP37 research upper deadband X23: %.0f ok", enabled);
    return true;
  }
  if (strncmp(cmd, "X26=", 4U) == 0) {
    float share = 0.0f;
    if (!parseValue(&cmd[4], &share) ||
        !tune(self, VP37_TUNING_RESEARCH_DESCENT_SHARE, share)) {
      return false;
    }
    deb("VP37 research descent share X26: %.3f ok", share);
    return true;
  }
  if (strncmp(cmd, "X25=", 4U) == 0) {
    float enabled = 0.0f;
    if (!parseValue(&cmd[4], &enabled) ||
        !tune(self, VP37_TUNING_RESEARCH_DESCENT_UNWIND, enabled)) {
      return false;
    }
    deb("VP37 research descent unwind X25: %.0f ok", enabled);
    return true;
  }
  if (strncmp(cmd, "X24=", 4U) == 0) {
    float enabled = 0.0f;
    if (!parseValue(&cmd[4], &enabled) ||
        !tune(self, VP37_TUNING_RESEARCH_RAMP_BRAKING, enabled)) {
      return false;
    }
    deb("VP37 research ramp braking X24: %.0f ok", enabled);
    return true;
  }
  if (strncmp(cmd, "X22=", 4U) == 0) {
    float enabled = 0.0f;
    if (!parseValue(&cmd[4], &enabled) ||
        !tune(self, VP37_TUNING_RESEARCH_RAMP_DAMPING, enabled)) {
      return false;
    }
    deb("VP37 research ramp damping X22: %.0f ok", enabled);
    return true;
  }
  if (strncmp(cmd, "X21=", 4U) == 0) {
    float weight = 1.0f;
    if (!parseValue(&cmd[4], &weight) ||
        !tune(self, VP37_TUNING_RESEARCH_REFERENCE_WEIGHT, weight)) {
      return false;
    }
    deb("VP37 research reference weight X21: %.3f ok", weight);
    return true;
  }
  if (strncmp(cmd, "X20=", 4U) == 0) {
    float enabled = 0.0f;
    if (!parseValue(&cmd[4], &enabled) ||
        !tune(self, VP37_TUNING_RESEARCH_SLOW_DAMPING, enabled)) {
      return false;
    }
    deb("VP37 research slow damping X20: %.0f ok", enabled);
    return true;
  }
  if (strncmp(cmd, "X18=", 4U) == 0) {
    float enabled = 0.0f;
    if (!parseValue(&cmd[4], &enabled) ||
        !tune(self, VP37_TUNING_RESEARCH_MOTION_INTEGRAL, enabled)) {
      return false;
    }
    deb("VP37 research motion integral X18: %.0f ok", enabled);
    return true;
  }
  if (strncmp(cmd, "X17=", 4U) == 0) {
    float mask = 0.0f;
    if (!parseValue(&cmd[4], &mask) ||
        !tune(self, VP37_TUNING_RESEARCH_FREEZE, mask)) {
      return false;
    }
    deb("VP37 research freeze X17: %.0f ok", mask);
    return true;
  }
  if (strncmp(cmd, "X12=", 4U) == 0) {
    char enabled = '\0';
    if (!parseSwitch(&cmd[3], &enabled) || !self->demand.atRest ||
        self->demand.requestedPercent != 0.0f || self->output.finalPWM != 0 ||
        self->thermal.observationEnabled ||
        self->thermal.driveCompensationEnabled ||
        self->currentControl.enabled || self->supply.cycleEnabled) {
      return false;
    }
    const hal_status_t status =
        enabled == '1' ? VP37_currentScanStart() : VP37_currentScanStop();
    if (status != HAL_OK) {
      return false;
    }
    self->scan.running = enabled == '1';
    self->scan.frameNs = VP37_currentScanFrameNs();
    self->scan.lastSequence = 0U;
    self->scan.cycleResultStatus = HAL_EAGAIN;
    self->scan.cycleResult.waveformValid = false;
    self->scan.cycleResult.profileValid = false;
    self->thermal.cycleValid = false;
    self->currentControl.matchFound = false;
    deb("VP37 research scan X12: %c ok", enabled);
    return true;
  }
  if (strncmp(cmd, "X13=", 4U) == 0) {
    float limit = 0.0f;
    if (!parseValue(&cmd[4], &limit) || !VP37_physicalLimitValid(limit) ||
        !self->demand.atRest || self->demand.requestedPercent != 0.0f ||
        self->output.finalPWM != 0) {
      return false;
    }
    self->demand.physicalLimitPercent = limit;
    deb("VP37 research limit X13: %.2f ok", limit);
    return true;
  }
  if (strncmp(cmd, "X14=", 4U) == 0) {
    float period = 0.0f;
    if (!parseValue(&cmd[4], &period) || !self->demand.atRest ||
        self->demand.requestedPercent != 0.0f || self->output.finalPWM != 0 ||
        (period != 0.0f && (period < 400.0f || period > 10000.0f))) {
      return false;
    }
    testsWorkersSetFixedCyclicPeriod((uint32_t)period);
    deb("VP37 research triangle X14: %.0f ms ok", period);
    return true;
  }
  if ((prefix == 'R') && (cmd[1] == '\0')) {
    applyDefaults(self);
    return true;
  }
  for (size_t i = 0U; i < COUNTOF(k_testsHelpersSwitches); i++) {
    if ((prefix == k_testsHelpersSwitches[i].prefix) &&
        parseSwitch(cmd, &digit)) {
      const VP37TuningParam param = k_testsHelpersSwitches[i].param;
      (void)tune(self, param, (digit == '1') ? 1.0f : 0.0f);
      deb("VP37 %s: %u", k_testsHelpersSwitches[i].name, switchOn(self, param));
      return true;
    }
  }
  if ((prefix == 'V') && (cmd[2] == '\0') &&
      ((cmd[1] == '0') || (cmd[1] == '1') || (cmd[1] == '2'))) {
    // V2 freezes the scale where it is: the supply loop stays open so a
    // supply-side oscillation can be told from one closed through the ECU.
    const bool frozen = cmd[1] == '2';
    (void)tune(self, VP37_TUNING_SUPPLY_FROZEN, frozen ? 1.0f : 0.0f);
    if (!frozen) {
      (void)tune(self, VP37_TUNING_CYCLE_VOLTAGE,
                 (cmd[1] == '1') ? 1.0f : 0.0f);
    }
    deb("VP37 cycle voltage: %u frozen:%u",
        switchOn(self, VP37_TUNING_CYCLE_VOLTAGE),
        switchOn(self, VP37_TUNING_SUPPLY_FROZEN));
    return true;
  }

  float value = 0.0f;
  if ((cmd[1] != '\0') && parseValue(&cmd[1], &value)) {
    return applyValue(self, prefix, value, outStart);
  }
  return false;
}
/* How long a changed demand waits for core 0 to print the announcement of
 * its step before it is applied regardless [ms]. */
#define STEP_EVENT_APPLY_TIMEOUT_MS 50U
#if (STEP_EVENT_APPLY_TIMEOUT_MS < 10U) || (STEP_EVENT_APPLY_TIMEOUT_MS > 500U)
#error "STEP_EVENT_APPLY_TIMEOUT_MS must be in 10..500 ms"
#endif

/* The demand applied last and, while a changed one waits for its announcement
 * to be printed, when the wait began. */
static float s_appliedDemand;
static bool s_holdingDemand;
static uint32_t s_holdStartedMs;

/**
 * @brief Keep the previous demand while the announcement of a new step waits
 * for core 0 to print it, so the log shows the step before the move.
 * @param demand Demand the running test produced this tick.
 * @return Demand to apply: the previous one during the wait, bounded by
 *         STEP_EVENT_APPLY_TIMEOUT_MS, the new one afterwards.
 */
static float holdDemandUntilAnnounced(float demand) {
  if (demand == s_appliedDemand) {
    s_holdingDemand = false;
  } else if (!testsWorkersStepEventPending()) {
    s_holdingDemand = false;
    s_appliedDemand = demand;
  } else {
    const uint32_t nowMs = hal_millis();
    if (!s_holdingDemand) {
      s_holdingDemand = true;
      s_holdStartedMs = nowMs;
    }
    if (hal_elapsed_u32(nowMs, s_holdStartedMs, STEP_EVENT_APPLY_TIMEOUT_MS)) {
      s_holdingDemand = false;
      s_appliedDemand = demand;
    }
  }
  return s_appliedDemand;
}

/** @brief Forget any wait: the demand just applied is the reference again. */
static void resetDemandHold(float applied) {
  s_appliedDemand = applied;
  s_holdingDemand = false;
}

static void moduleReset(void) {
  HAL_ATOMIC_STORE(&s_defaultsReplies, 0U, HAL_ATOMIC_RELEASE);
  testsWorkersReset();
}

static void modulePrepare(const fiesta_test_t *entry) {
  (void)entry;
  resetDemandHold(getECUContext()->injectionPump.demand.requestedPercent);
}

static void moduleCleanup(const fiesta_test_t *entry, const char *result,
                          bool restart) {
  (void)result;
  if (entry->drivesDemand && !restart) {
    (void)VP37_setPositionDemandPercentage(&getECUContext()->injectionPump,
                                           0.0f);
    resetDemandHold(0.0f);
  }
}

static void moduleApplyOutput(float demand) {
  (void)VP37_setPositionDemandPercentage(&getECUContext()->injectionPump,
                                         holdDemandUntilAnnounced(demand));
}

static void moduleReadDrive(fiesta_test_drive_t *out) {
  const VP37Pump *pump = &getECUContext()->injectionPump;
  const int32_t bottom = VP37_getPositionDemandMinValue(pump);
  const int32_t top = VP37_getPositionDemandMaxValue(pump);
  out->valid = pump->feedback.calibrationDone && (top > bottom) &&
               (pump->feedback.position >= 0);
  if (out->valid) {
    const float span = (float)(top - bottom);
    out->demand_x10 =
        (int32_t)lroundf(fmaxf(pump->demand.requestedPercent, 0.0f) * 10.0f);
    out->position_x10 = (int32_t)lroundf(
        (float)(pump->feedback.position - bottom) * 1000.0f / span);
  }
}

static const char *moduleScBlocked(void) {
#if ECU_SC_TESTS_RPM_INTERLOCK
  if (getGlobalValue(F_RPM) > (float)ECU_SC_TESTS_RPM_LIMIT) {
    return SC_TEST_RESULT_ENGINE_RUNNING;
  }
#endif
  return NULL;
}

static bool moduleCommand(const char *line, uint32_t *requested) {
  ecu_test_id_t test = START_TEST_NONE;
  const bool accepted =
      testsHelpersApplyCommand(&getECUContext()->injectionPump, line, &test);
  *requested = (uint32_t)test;
  return accepted;
}

static void moduleEvents(void) {
  if (HAL_ATOMIC_LOAD(&s_defaultsReplies, HAL_ATOMIC_ACQUIRE) != 0U) {
    deb(TEST_HIGHLIGHT_ON
        "PID reset to defaults: Kp=%.4f Ki=%.4f Kd=%.4f TU=%.1f "
        "TF=%.4f" TEST_HIGHLIGHT_OFF,
        VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, VP37_PID_TIME_UPDATE,
        VP37_PID_TF);
    (void)HAL_ATOMIC_FETCH_SUB(&s_defaultsReplies, 1U, HAL_ATOMIC_RELAXED);
  }
  testsWorkersPrintStepEvent();
}

bool initTests(void) {
  static const fiesta_test_config_t config = {
      .suite = testsSuite,
      .params = s_params,
      .values = s_paramValues,
      .param_count = COUNTOF(s_params),
      .keepalive_ms = ECU_SC_TESTS_KEEPALIVE_MS,
      .reset = moduleReset,
      .prepare = modulePrepare,
      .cleanup = moduleCleanup,
      .apply_output = moduleApplyOutput,
      .read_drive = moduleReadDrive,
      .sc_blocked = moduleScBlocked,
      .command = moduleCommand,
      .help = testsHelpersPrintParameters,
      .events = moduleEvents,
  };
  return fiesta_test_init(&config) == HAL_OK;
}

bool startTests(void) {
  return startTest(ECU_FUNCTIONAL_TESTS_AUTOSTART) == HAL_OK;
}
hal_status_t startTest(ecu_test_id_t test) {
  /* Unknown ECU IDs must not alias a reserved action of the shared runner. */
  const uint32_t id = test == START_TEST_ALL
                          ? FIESTA_TEST_ALL
                          : ((unsigned)test < (unsigned)START_TEST_COUNT
                                 ? (uint32_t)test
                                 : (uint32_t)START_TEST_COUNT);
  return fiesta_test_start(id);
}
hal_status_t stopTest(void) { return fiesta_test_skip(); }
hal_status_t stopTests(void) { return fiesta_test_stop(); }
bool tickTests(void) { return fiesta_test_tick(); }
void tickTestsHandleSerialLine(const char *line) { fiesta_test_console(line); }
const char *testsActiveName(void) { return fiesta_test_active_name(); }
void testsPrintEvents(void) { fiesta_test_events(); }
uint32_t testsCyclicDelayMs(void) {
  return fiesta_test_active_id() == START_TEST_CYCLIC
             ? testsWorkersCyclicDelayMs()
             : 0U;
}
void testsScSessionEnded(void) { fiesta_test_session_ended(); }
const struct sc_command_test_ops_s *testsScOps(void) {
  return fiesta_test_sc_ops();
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

void testsPrintEvents(void) {}

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
