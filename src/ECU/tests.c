
#include "tests.h"
#include "dtcManager.h"
#include "ecuContext.h"
#include "sensors.h"
#include "vp37.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Global state
//=============================================================================

static bool s_testsInitialized = false;

//=============================================================================
// VP37 Serial-session bench control
//=============================================================================
#ifdef START_TEST_ENABLE_VP37_TUNING

/**
 * @brief Parse one serial command for the active VP37 bench source.
 * @param self VP37 instance being tuned.
 * @param cmd Null-terminated command string.
 * @return None.
 */
static void VP37_processSerialCommand(VP37Pump *self, const char *cmd);

/**
 * @brief Apply a queued runtime VP37 bench command on the owner core.
 * @param self VP37 instance being tuned.
 * @return None.
 */
static void VP37_TunePID(VP37Pump *self);

static hal_mutex_t s_tuneMutex = NULL;
static char s_tuneCommand[VP37_CMD_BUF_SIZE];
static uint8_t s_tuneCommandLength;
static float s_commandDemand = 0.0f;

#endif

//=============================================================================
// VP37 Cyclic Test (START_TEST_ENABLE_VP37_CYCLIC)
//=============================================================================
#ifdef START_TEST_ENABLE_VP37_CYCLIC

static CyclicTest s_ct;
static uint32_t s_holdStartedMs;
static uint32_t s_holdDurationMs;

static const uint32_t s_cyclicDelaysMs[] = {
    CYCLIC_DELAYTIME_A, CYCLIC_DELAYTIME_B, CYCLIC_DELAYTIME_C,
    CYCLIC_DELAYTIME_D};

static uint32_t s_cycleIndex = 0U;
static uint32_t s_completedCycles = 0U;

uint32_t getCurrentVP37CyclicDelayMs(void) {
  return s_cyclicDelaysMs[s_cycleIndex];
}

static void VP37_resetCyclicTest(void) {
  s_ct.value = 0;
  s_ct.increment = 1;
  s_ct.previousMillis = hal_millis();
  s_cycleIndex = 0U;
  s_completedCycles = 0U;
}

/**
 * @brief Generate a repeating 0-100-0 throttle ramp for VP37 testing.
 * @return Current cyclic throttle demand.
 */
static float VP37cyclicTest(void) {
  if (s_commandDemand >= 0.0f) {
    if (s_commandDemand > 0.0f &&
        hal_millis_deadline_expired(s_holdStartedMs, s_holdDurationMs)) {
      s_commandDemand = 0.0f;
    }
    return s_commandDemand;
  }
  uint32_t currentMillis = hal_millis();

  if (currentMillis - s_ct.previousMillis >= getCurrentVP37CyclicDelayMs()) {
    s_ct.previousMillis = currentMillis;
    s_ct.value += s_ct.increment;

    if (s_ct.value >= 100) {
      s_ct.value = 100;
      s_ct.increment = -s_ct.increment;
    } else if (s_ct.value <= 0) {
      s_ct.value = 0;
      s_ct.increment = -s_ct.increment;
      s_completedCycles++;
      if (s_completedCycles >= CYCLIC_FULL_CYCLES) {
        s_completedCycles = 0U;
        s_cycleIndex++;
        if (s_cycleIndex >= COUNTOF(s_cyclicDelaysMs)) {
          s_cycleIndex = 0U;
        }
      }
    }
  }
  return s_ct.value;
}

#endif // START_TEST_ENABLE_VP37_CYCLIC

//=============================================================================
// Public Test Interface
//=============================================================================

/**
 * @brief Initialize enabled test fixtures and runtime state.
 * @return True when initialization finished.
 */
bool initTests(void) {
#ifdef START_TEST_ENABLE_VP37_TUNING
  if (s_tuneMutex == NULL) {
    s_tuneMutex = hal_mutex_create();
    if (s_tuneMutex == NULL) {
      return false;
    }
  }
  s_tuneCommandLength = 0U;
  s_tuneCommand[0] = '\0';
  s_commandDemand = 0.0f;
#endif

#ifdef START_TEST_ENABLE_VP37_CYCLIC
  VP37_resetCyclicTest();
  s_holdStartedMs = 0U;
  s_holdDurationMs = 0U;
#endif

  s_testsInitialized = true;
  return true;
}

/**
 * @brief Execute one-shot startup diagnostics enabled at compile time.
 * @return True when startup tests finished.
 */
bool startTests(void) {
#ifdef START_TEST_ENABLE_DTC_INJECTION
  static bool dtcInjected = false;
  if (!dtcInjected) {
    const uint16_t code = (uint16_t)DTC_PCF8574_COMM_FAIL;
    dtcManagerSetActive(code, true);
    deb("TEST: startup DTC injected: 0x%04X (%s)", (unsigned)code,
        dtcManagerGetName(code));
    dtcInjected = true;
  }
#endif

  return true;
}

/**
 * @brief Execute one periodic step of enabled runtime tests.
 * @return None.
 */
void tickTests(void) {
#ifdef START_TEST_ENABLE_VP37_TUNING
  if (!s_testsInitialized) {
    return;
  }

#ifdef START_TEST_ENABLE_VP37_CYCLIC
  const float demand = VP37cyclicTest();
#else
  const float demand = s_commandDemand;
#endif
  ecu_context_t *ctx = getECUContext();
  VP37_setVP37Throttle(&ctx->injectionPump, demand);
  VP37_TunePID(&ctx->injectionPump);
#endif
}

/**
 * @brief Forward one already-parsed serial command line to enabled test
 *        fixtures. See tests.h for full description.
 * @param line NUL-terminated command line.
 * @return None.
 */
void tickTestsHandleSerialLine(const char *line) {
#ifdef START_TEST_ENABLE_VP37_TUNING
  if (!s_testsInitialized || line == NULL || line[0] == '\0') {
    return;
  }
  const size_t length = strlen(line);
  if (length >= COUNTOF(s_tuneCommand)) {
    derr("PID command too long");
    return;
  }
  hal_mutex_lock(s_tuneMutex);
  const bool busy = s_tuneCommandLength != 0U;
  if (!busy) {
    (void)memcpy(s_tuneCommand, line, length + 1U);
    s_tuneCommandLength = (uint8_t)length;
  }
  hal_mutex_unlock(s_tuneMutex);
  if (busy) {
    derr("PID command pending; retry after acknowledgement");
  }
#else
  (void)line;
#endif
}

#ifdef START_TEST_ENABLE_VP37_TUNING
/**
 * @brief Apply one queued command on the core that owns PID state.
 * @param self Controller updated under the caller's VP37 mutex.
 * @return None.
 */
static void VP37_TunePID(VP37Pump *self) {
  char command[VP37_CMD_BUF_SIZE];
  hal_mutex_lock(s_tuneMutex);
  const uint8_t length = s_tuneCommandLength;
  if (length != 0U) {
    (void)memcpy(command, s_tuneCommand, (size_t)length + 1U);
    s_tuneCommandLength = 0U;
  }
  hal_mutex_unlock(s_tuneMutex);
  if (length != 0U) {
    VP37_processSerialCommand(self, command);
  }
}

/**
 * @brief Decode one textual PID-tuning command and apply it to VP37.
 * @param self VP37 instance under test.
 * @param cmd Null-terminated command string.
 * @return None.
 */
static void VP37_processSerialCommand(VP37Pump *self, const char *cmd) {
  if (cmd[0] == '?' || cmd[0] == 'H' || cmd[0] == 'h') {
    float kp, ki, kd;
    VP37_getVP37PIDValues(self, &kp, &ki, &kd);
    deb("\033[33mPID: Kp=%.4f Ki=%.4f Kd=%.4f TU=%.1f TF=%.4f\033[0m", kp, ki,
        kd, self->pidTimeUpdate, self->pidTf);
    deb("\033[33mCAL: MIN=%d MID=%d MAX=%d\033[0m", self->VP37_ADJUST_MIN,
        self->VP37_ADJUST_MIDDLE, self->VP37_ADJUST_MAX);
#ifdef START_TEST_ENABLE_VP37_CYCLIC
    deb("\033[33mSerial Session payloads: P<val> I<val> D<val> T<ms> "
        "F<seconds> R(reset) B(trace) L<PWM cap;0=auto> "
        "W<thermal weight 0..1> Q<current observation 0|1> V<cycle voltage "
        "0|1|2=freeze> M<measured drive compensation 0|1> K<map trim 0|1> "
        "N<integral deadband top Hz> "
        "E<hold confirmation ms> "
        "S<timed hold 0..100> C(cyclic) X(stop) ?(help)\033[0m");
#else
    deb("\033[33mSerial Session payloads: P<val> I<val> D<val> T<ms> "
        "F<seconds> R(reset) B(trace) L<PWM cap;0=auto> "
        "W<thermal weight 0..1> Q<current observation 0|1> V<cycle voltage "
        "0|1|2=freeze> M<measured drive compensation 0|1> K<map trim 0|1> "
        "N<integral deadband top Hz> "
        "E<hold confirmation ms> "
        "S<persistent demand 0..100> X(stop) ?(help)\033[0m");
#endif
    return;
  }

  if (((cmd[0] == 'B') || (cmd[0] == 'b')) && (cmd[1] == '\0')) {
    const hal_status_t status = VP37_startTrace(self);
    deb("VP37 trace: %s samples:%u", hal_status_to_string(status),
        (unsigned int)VP37_TRACE_SAMPLES);
    return;
  }

  if ((cmd[0] == 'X' || cmd[0] == 'x') && cmd[1] == '\0') {
    VP37_stop(self);
    deb("VP37 stopped; restart ECU to initialize");
    return;
  }
#ifdef START_TEST_ENABLE_VP37_CYCLIC
  if ((cmd[0] == 'C' || cmd[0] == 'c') && cmd[1] == '\0') {
    VP37_resetCyclicTest();
    s_commandDemand = -1.0f;
    deb("VP37 cyclic started: delay:%lu ms cycles:%u",
        (unsigned long)getCurrentVP37CyclicDelayMs(),
        (unsigned int)CYCLIC_FULL_CYCLES);
    return;
  }
#endif

  if (((cmd[0] == 'Q') || (cmd[0] == 'q')) &&
      ((cmd[1] == '0') || (cmd[1] == '1')) && (cmd[2] == '\0')) {
    self->currentObservationEnabled = cmd[1] == '1';
    deb("VP37 current observation: %u",
        self->currentObservationEnabled ? 1U : 0U);
    return;
  }

  if (((cmd[0] == 'V') || (cmd[0] == 'v')) &&
      ((cmd[1] == '0') || (cmd[1] == '1') || (cmd[1] == '2')) &&
      (cmd[2] == '\0')) {
    // V2 freezes the scale where it is: the supply loop stays open so a
    // supply-side oscillation can be told from one closed through the ECU.
    self->voltageFrozen = cmd[1] == '2';
    if (!self->voltageFrozen) {
      self->cycleVoltageEnabled = cmd[1] == '1';
    }
    deb("VP37 cycle voltage: %u frozen:%u", self->cycleVoltageEnabled ? 1U : 0U,
        self->voltageFrozen ? 1U : 0U);
    return;
  }

  if (((cmd[0] == 'K') || (cmd[0] == 'k')) &&
      ((cmd[1] == '0') || (cmd[1] == '1')) && (cmd[2] == '\0')) {
    self->mapTrimEnabled = cmd[1] == '1';
    for (uint32_t i = 0U; i < COUNTOF(self->mapTrim); i++) {
      self->mapTrim[i] = 0.0f;
    }
    self->mapTrimTransfers = 0U;
    deb("VP37 map trim: %u", self->mapTrimEnabled ? 1U : 0U);
    return;
  }

  if (((cmd[0] == 'M') || (cmd[0] == 'm')) &&
      ((cmd[1] == '0') || (cmd[1] == '1')) && (cmd[2] == '\0')) {
    self->driveCompensationEnabled = cmd[1] == '1';
    if (!self->driveCompensationEnabled) {
      self->driveSamples = 0U;
      self->driveResistanceReady = false;
      self->driveCompensationUsed = false;
      self->driveCorrection = 1.0f;
    }
    deb("VP37 drive compensation: %u",
        self->driveCompensationEnabled ? 1U : 0U);
    return;
  }

  if (((cmd[0] == 'R') || (cmd[0] == 'r')) && (cmd[1] == '\0')) {
    VP37_setVP37PID(self, VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, true);
    self->pidTimeUpdate = VP37_PID_TIME_UPDATE;
    self->pidTf = VP37_PID_TF;
    self->pidIntegralOverride = VP37_BENCH_INTEGRAL_CAP_PWM;
    self->temperatureCompensationWeight = 1.0f;
    self->integralHoldConfirmMs = VP37_INTEGRAL_HOLD_CONFIRM_MS;
    self->integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
    for (uint32_t i = 0U; i < COUNTOF(self->mapTrim); i++) {
      self->mapTrim[i] = 0.0f;
    }
    self->mapTrimTransfers = 0U;
    hal_pid_controller_set_tf(self->adjustController, self->pidTf);
    deb("\033[33mPID reset to defaults: Kp=%.4f Ki=%.4f Kd=%.4f TU=%.1f "
        "TF=%.4f\033[0m",
        VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, VP37_PID_TIME_UPDATE,
        VP37_PID_TF);
    return;
  }

  // Gains use seconds; T selects the minimum control period in milliseconds.
  const char prefix = cmd[0];
  if ((prefix == 'P' || prefix == 'p' || prefix == 'I' || prefix == 'i' ||
       prefix == 'D' || prefix == 'd' || prefix == 'T' || prefix == 't' ||
       prefix == 'F' || prefix == 'f' || prefix == 'L' || prefix == 'l' ||
       prefix == 'S' || prefix == 's' || prefix == 'W' || prefix == 'w' ||
       prefix == 'E' || prefix == 'e' || prefix == 'N' || prefix == 'n') &&
      cmd[1] != '\0') {

    char *end = NULL;
    errno = 0;
    const float val = strtof(&cmd[1], &end);
    if ((errno != 0) || (end == &cmd[1]) || (*end != '\0') || !isfinite(val) ||
        (val < 0.0f) ||
        (((prefix == 'T') || (prefix == 't')) &&
         ((val < 1.0f) || (val > 100.0f))) ||
        (((prefix == 'S') || (prefix == 's')) && (val > 100.0f)) ||
        (((prefix == 'E') || (prefix == 'e')) &&
         ((val > 1000.0f) || (val != floorf(val)))) ||
        (((prefix == 'W') || (prefix == 'w')) && (val > 1.0f)) ||
        (((prefix == 'L') || (prefix == 'l')) &&
         (val > VP37_BENCH_INTEGRAL_LIMIT_MAX)) ||
        (((prefix == 'N') || (prefix == 'n')) &&
         (val > VP37_PID_DEADBAND_TOP_MAX_HZ))) {
      derr("Invalid PID setting: '%s'", cmd);
      return;
    }

    switch (prefix) {
    case 'N':
    case 'n':
      self->integralDeadbandTopHz = val;
      deb("VP37 integral deadband top: %.0f Hz", val);
      break;
    case 'E':
    case 'e':
      self->integralHoldConfirmMs = (uint32_t)val;
      self->integralHoldEnterPending = false;
      deb("VP37 hold confirmation: %lu ms",
          (unsigned long)self->integralHoldConfirmMs);
      break;
    case 'W':
    case 'w':
      self->temperatureCompensationWeight = val;
      deb("VP37 thermal weight: %.2f", val);
      break;
    case 'L':
    case 'l':
      self->pidIntegralOverride = val;
      deb("VP37 integral cap: %.1f (0=position profile)", val);
      break;
    case 'S':
    case 's':
      s_commandDemand = val;
#ifdef START_TEST_ENABLE_VP37_CYCLIC
      s_holdStartedMs = hal_millis();
      s_holdDurationMs = val >= VP37_BENCH_HIGH_HOLD_PERCENT
                             ? VP37_BENCH_HIGH_HOLD_MS
                             : VP37_BENCH_HOLD_MS;
      deb("VP37 hold: %.1f timeout:%lu ms (then zero)", val,
          (unsigned long)(val > 0.0f ? s_holdDurationMs : 0U));
#else
      deb("VP37 demand: %.1f persistent", val);
#endif
      break;
    case 'P':
    case 'p':
      VP37_setVP37PID(self, val, self->pidKi, self->pidKd, false);
      deb("\033[33mKp = %.4f\033[0m", val);
      break;
    case 'I':
    case 'i':
      VP37_setVP37PID(self, self->pidKp, val, self->pidKd, false);
      deb("\033[33mKi = %.4f\033[0m", val);
      break;
    case 'D':
    case 'd':
      VP37_setVP37PID(self, self->pidKp, self->pidKi, val, false);
      deb("\033[33mKd = %.4f\033[0m", val);
      break;
    case 'T':
    case 't':
      self->pidTimeUpdate = val;
      deb("\033[33mTU = %.1f\033[0m", val);
      break;
    case 'F':
    case 'f':
      self->pidTf = val;
      hal_pid_controller_set_tf(self->adjustController, val);
      deb("\033[33mTF = %.4f\033[0m", val);
      break;
    default:
      break;
    }
    return;
  }

  derr("Unknown command: '%s'. Send ? for help.", cmd);
}

#endif
