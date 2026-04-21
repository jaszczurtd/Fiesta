
#include "tests.h"
#include "dtcManager.h"
#include "sensors.h"
#include "vp37.h"
#include "ecuContext.h"

//=============================================================================
// Global state
//=============================================================================

static bool s_testsInitialized = false;

//=============================================================================
// VP37 Cyclic Test (START_TEST_ENABLE_VP37_CYCLIC)
//=============================================================================
#ifdef START_TEST_ENABLE_VP37_CYCLIC

/**
 * @brief Parse one serial PID-tuning command for the VP37 cyclic test.
 * @param self VP37 instance being tuned.
 * @param cmd Null-terminated command string.
 * @return None.
 */
void VP37_processSerialCommand(VP37Pump *self, const char *cmd);

/**
 * @brief Poll the serial console and apply runtime VP37 tuning commands.
 * @param self VP37 instance being tuned.
 * @return None.
 */
void VP37_TunePID(VP37Pump *self);

static CyclicTest s_ct;

/**
 * @brief Generate a repeating 0-100-0 throttle ramp for VP37 testing.
 * @return Current cyclic throttle demand.
 */
static int VP37cyclicTest(void) {
  uint32_t currentMillis = hal_millis();

  if(currentMillis - s_ct.previousMillis >= CYCLIC_DELAYTIME) {
    s_ct.previousMillis = currentMillis;
    s_ct.value += s_ct.increment;

    if(s_ct.value >= 100 || s_ct.value <= 0) {
      s_ct.increment = -s_ct.increment;
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
#ifdef START_TEST_ENABLE_VP37_CYCLIC
  s_ct.cmdLen = 0;
  s_ct.cmdBuf[0] = '\0';

  s_ct.increment = 1;
  s_ct.uv = 0.001f;
  s_ct.value = 0;
  s_ct.previousMillis = hal_millis();  
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
  if(!dtcInjected) {
    const uint16_t code = (uint16_t)DTC_PCF8574_COMM_FAIL;
    dtcManagerSetActive(code, true);
    deb("TEST: startup DTC injected: 0x%04X (%s)", (unsigned)code, getDtcName(code));
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
#ifdef START_TEST_ENABLE_VP37_CYCLIC
  if(!s_testsInitialized) {
    return;
  }

  int thr = VP37cyclicTest();
  ecu_context_t *ctx = getECUContext();
  VP37_setVP37Throttle(&ctx->injectionPump, thr);
  VP37_TunePID(&ctx->injectionPump);

#endif
}

#ifdef START_TEST_ENABLE_VP37_CYCLIC
/**
 * @brief Poll the serial console and apply live PID tuning commands.
 * @param self VP37 instance under test.
 * @return None.
 */
void VP37_TunePID(VP37Pump *self) {
  // ── Serial PID tuner: read commands from USB CDC ────────────────────
  while(hal_serial_available() > 0) {
    int ch = hal_serial_read();
    if(ch < 0) break;

    if(ch == '\n' || ch == '\r') {
      if(s_ct.cmdLen > 0) {
        s_ct.cmdBuf[s_ct.cmdLen] = '\0';
        VP37_processSerialCommand(self, s_ct.cmdBuf);
        s_ct.cmdLen = 0;
      }
    } else if(s_ct.cmdLen < VP37_CMD_BUF_SIZE - 1) {
      s_ct.cmdBuf[s_ct.cmdLen++] = (char)ch;
    }
  }
}

/**
 * @brief Decode one textual PID-tuning command and apply it to VP37.
 * @param self VP37 instance under test.
 * @param cmd Null-terminated command string.
 * @return None.
 */
void VP37_processSerialCommand(VP37Pump *self, const char *cmd) {
  float val;

  if(cmd[0] == '?' || cmd[0] == 'H' || cmd[0] == 'h') {
    float kp, ki, kd;
    VP37_getVP37PIDValues(self, &kp, &ki, &kd);
    deb("\033[33mPID: Kp=%.4f Ki=%.4f Kd=%.4f TU=%.1f TF=%.4f\033[0m",
        kp, ki, kd, self->pidTimeUpdate, self->pidTf);
    deb("\033[33mCAL: MIN=%d MID=%d MAX=%d\033[0m", self->VP37_ADJUST_MIN,
        self->VP37_ADJUST_MIDDLE, self->VP37_ADJUST_MAX);
    deb("\033[33mCMD: P<val> I<val> D<val> T<val> F<val> R(reset) ?(help)\033[0m");
    return;
  }

  if(cmd[0] == 'R' || cmd[0] == 'r') {
    hal_pid_controller_set_kp(self->adjustController, VP37_PID_KP);
    hal_pid_controller_set_ki(self->adjustController, VP37_PID_KI);
    hal_pid_controller_set_kd(self->adjustController, VP37_PID_KD);
    self->pidTimeUpdate = VP37_PID_TIME_UPDATE;
    self->pidTf = VP37_PID_TF;
    hal_pid_controller_set_tf(self->adjustController, self->pidTf);
    hal_pid_controller_reset(self->adjustController);
    self->lastPWMval = -1;
    self->finalPWM = VP37_PWM_MIN;
    deb("\033[33mPID reset to defaults: Kp=%.4f Ki=%.4f Kd=%.4f TU=%.1f TF=%.4f\033[0m",
        VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, VP37_PID_TIME_UPDATE, VP37_PID_TF);
    return;
  }

  // Parse single-letter commands: P0.42  I0.11  D0.02  T80  F0.068
  char prefix = cmd[0];
  if((prefix == 'P' || prefix == 'p' ||
      prefix == 'I' || prefix == 'i' ||
      prefix == 'D' || prefix == 'd' ||
      prefix == 'T' || prefix == 't' ||
      prefix == 'F' || prefix == 'f') && cmd[1] != '\0') {

    val = (float)atof(&cmd[1]);

    switch(prefix) {
      case 'P': case 'p':
        hal_pid_controller_set_kp(self->adjustController, val);
        deb("\033[33mKp = %.4f\033[0m", val);
        break;
      case 'I': case 'i':
        hal_pid_controller_set_ki(self->adjustController, val);
        deb("\033[33mKi = %.4f\033[0m", val);
        break;
      case 'D': case 'd':
        hal_pid_controller_set_kd(self->adjustController, val);
        deb("\033[33mKd = %.4f\033[0m", val);
        break;
      case 'T': case 't':
        self->pidTimeUpdate = val;
        deb("\033[33mTU = %.1f\033[0m", val);
        break;
      case 'F': case 'f':
        self->pidTf = val;
        hal_pid_controller_set_tf(self->adjustController, val);
        deb("\033[33mTF = %.4f\033[0m", val);
        break;
    }
    return;
  }

  derr("Unknown command: '%s'. Send ? for help.", cmd);
}

#endif