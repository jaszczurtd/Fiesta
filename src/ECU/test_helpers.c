#include "test_helpers.h"

#if ECU_FUNCTIONAL_TESTS_ENABLED

#include <hal/core/hal_mutex_once.h>

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Console command queue
//=============================================================================

static hal_mutex_t s_commandMutex = NULL;
static char s_command[VP37_CMD_BUF_SIZE];
static uint8_t s_commandLength;

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
  uint32_t passLimit;
} s_cyclic;

static struct {
  uint32_t state;
  uint32_t startedMs;
  uint32_t holdStartedMs;
  uint32_t durationS;
  uint32_t holdS;
  float demand;
} s_random;

static struct {
  uint32_t startedMs;
  uint32_t holdMs;
  float demand;
} s_manual;

//=============================================================================
// Lifecycle
//=============================================================================

bool testHelpersReset(void) {
  s_commandLength = 0U;
  s_command[0] = '\0';

  s_cyclic.passLimit = CYCLIC_PASSES_DEFAULT;
  testHelpersCyclicStart();

  s_random.durationS = RANDOM_DURATION_S_DEFAULT;
  s_random.holdS = RANDOM_HOLD_S_DEFAULT;
  testHelpersRandomStart();

  s_manual.holdMs = VP37_BENCH_HOLD_MS_DEFAULT;
  s_manual.demand = 0.0f;
  s_manual.startedMs = hal_millis();

  return jh_hal_mutex_try_create_once(&s_commandMutex) != NULL;
}

hal_status_t testHelpersQueueCommand(const char *line) {
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
    s_commandLength = (uint8_t)length;
  }
  hal_mutex_unlock(s_commandMutex);
  return busy ? HAL_EBUSY : HAL_OK;
}

bool testHelpersTakeCommand(char *out) {
  if ((out == NULL) ||
      (jh_hal_mutex_try_create_once(&s_commandMutex) == NULL)) {
    return false;
  }
  hal_mutex_lock(s_commandMutex);
  const uint8_t length = s_commandLength;
  if (length != 0U) {
    (void)memcpy(out, s_command, (size_t)length + 1U);
    s_commandLength = 0U;
  }
  hal_mutex_unlock(s_commandMutex);
  return length != 0U;
}

//=============================================================================
// Cyclic ramp
//=============================================================================

void testHelpersCyclicStart(void) {
  s_cyclic.value = 0;
  s_cyclic.increment = 1;
  s_cyclic.previousMs = hal_millis();
  s_cyclic.profile = 0U;
  s_cyclic.cycles = 0U;
  s_cyclic.passes = 0U;
}

uint32_t testHelpersCyclicDelayMs(void) {
  return s_cyclicDelaysMs[s_cyclic.profile];
}

float testHelpersCyclicStep(bool *outFinished) {
  *outFinished = false;
  const uint32_t nowMs = hal_millis();
  if (!hal_elapsed_u32(nowMs, s_cyclic.previousMs,
                       testHelpersCyclicDelayMs())) {
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
    if (s_cyclic.cycles >= CYCLIC_FULL_CYCLES) {
      s_cyclic.cycles = 0U;
      s_cyclic.profile++;
      if (s_cyclic.profile >= COUNTOF(s_cyclicDelaysMs)) {
        s_cyclic.profile = 0U;
        s_cyclic.passes++;
        *outFinished = s_cyclic.passes >= s_cyclic.passLimit;
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

void testHelpersRandomStart(void) {
  s_random.state = RANDOM_SEED;
  s_random.startedMs = hal_millis();
  drawRandomDemand();
}

float testHelpersRandomStep(bool *outFinished) {
  *outFinished = hal_elapsed_u32(hal_millis(), s_random.startedMs,
                                 s_random.durationS * 1000U);
  if (*outFinished) {
    s_random.demand = 0.0f;
  } else if (hal_elapsed_u32(hal_millis(), s_random.holdStartedMs,
                             s_random.holdS * 1000U)) {
    drawRandomDemand();
  } else {
    // Still inside the hold of the current position.
  }
  return s_random.demand;
}

//=============================================================================
// Manual hold
//=============================================================================

void testHelpersManualStart(void) { s_manual.startedMs = hal_millis(); }

float testHelpersManualStep(bool *outFinished) {
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
// Parameter commands
//=============================================================================

void testHelpersPrintParameters(void) {
  deb(TEST_HIGHLIGHT_ON
      "Parameters: P<Kp> I<Ki> D<Kd> T<period ms> F<TF s> R(reset) "
      "B(trace) L<PWM cap;0=auto> W<thermal weight 0..1> "
      "Q<current observation 0|1> V<supply 0=local|1=period|2=freeze> "
      "M<measured drive compensation 0|1> K<map trim 0|1>" TEST_HIGHLIGHT_OFF);
  deb(TEST_HIGHLIGHT_ON
      "Parameters: N<integral deadband top Hz> U<motion boost up> "
      "J<motion boost down> E<hold confirmation ms> S<demand 0..100> "
      "G<demand auto-zero ms;0=hold> Z<cyclic passes> Y<random seconds> "
      "A<random hold seconds> X(stop actuator)" TEST_HIGHLIGHT_OFF);
}

char testHelpersUpper(char value) {
  return ((value >= 'a') && (value <= 'z')) ? (char)(value - ('a' - 'A'))
                                            : value;
}

bool testHelpersEquals(const char *text, const char *word) {
  if ((text == NULL) || (word == NULL)) {
    return false;
  }
  size_t i = 0U;
  while ((text[i] != '\0') && (word[i] != '\0')) {
    if (testHelpersUpper(text[i]) != testHelpersUpper(word[i])) {
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
  self->pid.pidTf = VP37_PID_TF;
  self->pid.pidIntegralOverride = VP37_BENCH_INTEGRAL_CAP_PWM;
  self->thermal.temperatureCompensationWeight = 1.0f;
  self->pid.integralHoldConfirmMs = VP37_INTEGRAL_HOLD_CONFIRM_MS;
  self->pid.integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  self->feedforward.motionBoostUp = VP37_PWM_FF_MOTION_BOOST_DEFAULT;
  self->feedforward.motionBoostDown = VP37_PWM_FF_DESCENT_BOOST;
  for (uint32_t i = 0U; i < COUNTOF(self->feedforward.mapTrim); i++) {
    self->feedforward.mapTrim[i] = 0.0f;
  }
  self->feedforward.mapTrimTransfers = 0U;
  hal_pid_controller_set_tf(self->pid.adjustController, self->pid.pidTf);
  s_cyclic.passLimit = CYCLIC_PASSES_DEFAULT;
  s_random.durationS = RANDOM_DURATION_S_DEFAULT;
  s_random.holdS = RANDOM_HOLD_S_DEFAULT;
  s_manual.holdMs = VP37_BENCH_HOLD_MS_DEFAULT;
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
  switch (prefix) {
  case 'P':
    VP37_setVP37PID(self, value, self->pid.pidKi, self->pid.pidKd, false);
    deb(TEST_HIGHLIGHT_ON "Kp = %.4f" TEST_HIGHLIGHT_OFF, value);
    break;
  case 'I':
    VP37_setVP37PID(self, self->pid.pidKp, value, self->pid.pidKd, false);
    deb(TEST_HIGHLIGHT_ON "Ki = %.4f" TEST_HIGHLIGHT_OFF, value);
    break;
  case 'D':
    VP37_setVP37PID(self, self->pid.pidKp, self->pid.pidKi, value, false);
    deb(TEST_HIGHLIGHT_ON "Kd = %.4f" TEST_HIGHLIGHT_OFF, value);
    break;
  case 'T':
    if ((value < 1.0f) || (value > 100.0f)) {
      return false;
    }
    self->pidTimeUpdate = value;
    deb(TEST_HIGHLIGHT_ON "TU = %.1f" TEST_HIGHLIGHT_OFF, value);
    break;
  case 'F':
    self->pid.pidTf = value;
    hal_pid_controller_set_tf(self->pid.adjustController, value);
    deb(TEST_HIGHLIGHT_ON "TF = %.4f" TEST_HIGHLIGHT_OFF, value);
    break;
  case 'L':
    if (value > VP37_BENCH_INTEGRAL_LIMIT_MAX) {
      return false;
    }
    self->pid.pidIntegralOverride = value;
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
    s_manual.demand = value;
    s_manual.startedMs = hal_millis();
    *outStart = START_TEST_MANUAL;
    deb("VP37 demand: %.1f auto-zero:%lu ms", value,
        (unsigned long)s_manual.holdMs);
    break;
  case 'G':
    if (value > VP37_BENCH_HOLD_MS_MAX) {
      return false;
    }
    s_manual.holdMs = (uint32_t)value;
    deb("VP37 demand auto-zero: %lu ms (0=hold)",
        (unsigned long)s_manual.holdMs);
    break;
  case 'Z':
    if ((value < 1.0f) || (value > CYCLIC_PASSES_MAX)) {
      return false;
    }
    s_cyclic.passLimit = (uint32_t)value;
    deb("VP37 cyclic passes: %lu", (unsigned long)s_cyclic.passLimit);
    break;
  case 'Y':
    if ((value < 1.0f) || (value > RANDOM_DURATION_S_MAX)) {
      return false;
    }
    s_random.durationS = (uint32_t)value;
    deb("VP37 random duration: %lu s", (unsigned long)s_random.durationS);
    break;
  case 'A':
    if ((value < 1.0f) || (value > RANDOM_HOLD_S_MAX)) {
      return false;
    }
    s_random.holdS = (uint32_t)value;
    deb("VP37 random hold: %lu s", (unsigned long)s_random.holdS);
    break;
  default:
    return false;
  }
  return true;
}

bool testHelpersApplyCommand(VP37Pump *self, const char *cmd,
                             ecu_test_id_t *outStart) {
  *outStart = START_TEST_NONE;
  const char prefix = testHelpersUpper(cmd[0]);
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
  if ((prefix == 'R') && (cmd[1] == '\0')) {
    applyDefaults(self);
    return true;
  }
  if ((prefix == 'Q') && parseSwitch(cmd, &digit)) {
    self->thermal.currentObservationEnabled = digit == '1';
    deb("VP37 current observation: %u",
        self->thermal.currentObservationEnabled ? 1U : 0U);
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
    self->supply.voltageFrozen = cmd[1] == '2';
    if (!self->supply.voltageFrozen) {
      self->supply.cycleVoltageEnabled = cmd[1] == '1';
    }
    deb("VP37 cycle voltage: %u frozen:%u",
        self->supply.cycleVoltageEnabled ? 1U : 0U,
        self->supply.voltageFrozen ? 1U : 0U);
    return true;
  }

  float value = 0.0f;
  if ((cmd[1] != '\0') && parseValue(&cmd[1], &value)) {
    return applyValue(self, prefix, value, outStart);
  }
  return false;
}

#endif /* ECU_FUNCTIONAL_TESTS_ENABLED */
