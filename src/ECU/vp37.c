#include "vp37.h"
#include <hal/hal_soft_timer.h>

static void VP37_processSerialCommand(VP37Pump *self, const char *cmd);

static int32_t VP37_getMaxAdjustometerPWMVal(VP37Pump *self) {
  (void)self;
  return hal_map(VP37_CALIBRATION_MAX_PERCENTAGE, 0, 100, 0, PWM_RESOLUTION);
}

int32_t VP37_getAdjustometer(void) {
  adjustometer_reading_t *reading = getVP37Adjustometer();
  if(reading == NULL || !reading->commOk) {
    return -1;
  }
  return reading->pulseHz;
}

static int32_t VP37_getAdjustometerStable(VP37Pump *self) {
  int32_t count = STABILITY_ADJUSTOMETER_TAB_SIZE;
  if(count <= 0) {
    return VP37_getAdjustometer();
  }

  int32_t firstValue = VP37_getAdjustometer();
  int32_t sum = firstValue;
  int32_t minVal = firstValue;
  int32_t maxVal = firstValue;
  self->adjustStabilityTable[0] = firstValue;

  for(int32_t a = 1; a < count; a++) {
    int32_t value = VP37_getAdjustometer();
    self->adjustStabilityTable[a] = value;
    sum += value;

    if(value < minVal) {
      minVal = value;
    }
    if(value > maxVal) {
      maxVal = value;
    }
  }

  if(count > 2) {
    sum -= minVal;
    sum -= maxVal;
    return sum / (count - 2);
  }

  return getAverageFrom(self->adjustStabilityTable, STABILITY_ADJUSTOMETER_TAB_SIZE);
}

static void VP37_updateAdjustometerPosition(VP37Pump *self) {
  adjustometer_reading_t *reading = getVP37Adjustometer();
  if(reading == NULL) {
    return;
  }
  if(reading->commOk) {
    self->currentAdjustometerPosition = reading->pulseHz;
    self->adjCommLostSince = 0;

    setGlobalValue(F_FUEL_TEMP, reading->fuelTempC);
    setGlobalValue(F_VOLTS, reading->voltageRaw * 0.1f);
  } else {
    if(self->adjCommLostSince == 0) {
      self->adjCommLostSince = hal_millis();
    }
    derr("Failed to read adjustometer for VP37 control");
  }
}

static void VP37_applyDelay(void) {
  hal_delay_ms(VP37_ADJUST_TIMER);
  watchdog_feed();
}

static int32_t VP37_makeCalibrationValue(VP37Pump *self) {
  VP37_applyDelay();
  int32_t val = VP37_getAdjustometerStable(self);
  VP37_applyDelay();
  return val;
}

static void VP37_makeCalibration(VP37Pump *self) {
  self->VP37_ADJUST_MAX = self->VP37_ADJUST_MIDDLE = self->VP37_ADJUST_MIN = self->VP37_OPERATE_MAX = -1;

  valToPWM(PIO_VP37_RPM, VP37_getMaxAdjustometerPWMVal(self));
  self->VP37_ADJUST_MAX = VP37_makeCalibrationValue(self);
  valToPWM(PIO_VP37_RPM, 0);
  self->VP37_ADJUST_MIN = VP37_makeCalibrationValue(self);
  self->VP37_ADJUST_MIDDLE = ((self->VP37_ADJUST_MAX - self->VP37_ADJUST_MIN) / 2) + self->VP37_ADJUST_MIN;
  self->calibrationDone = (self->VP37_ADJUST_MAX > self->VP37_ADJUST_MIN) &&
                          (self->VP37_ADJUST_MIDDLE > 0);
  if(self->VP37_ADJUST_MAX <= self->VP37_ADJUST_MIN) {
    derr("VP37 calibration inverted: MIN=%d >= MAX=%d (oscillator not stable?)",
         self->VP37_ADJUST_MIN, self->VP37_ADJUST_MAX);
  }

  hal_pid_controller_set_output_limits(self->adjustController,
                                       (float)self->VP37_ADJUST_MIN,
                                       (float)self->VP37_ADJUST_MAX);
  deb("VP37 calibration: MIN=%d MIDDLE=%d MAX=%d OPERATE_MAX=%d", 
      self->VP37_ADJUST_MIN, 
      self->VP37_ADJUST_MIDDLE, 
      self->VP37_ADJUST_MAX, 
      self->VP37_OPERATE_MAX);
}

static void VP37_throttleCycle(VP37Pump *self) {
  if(self->desiredAdjustometer < 0) {
    return;
  }

  hal_pid_controller_update_time(self->adjustController, self->pidTimeUpdate);

  self->pidErr = self->desiredAdjustometer - self->currentAdjustometerPosition;

  float output = hal_pid_controller_update(self->adjustController, (float)self->pidErr);
  self->pwmValue = mapfloat(output, self->VP37_ADJUST_MIN, self->VP37_ADJUST_MAX, VP37_PWM_MIN, VP37_PWM_MAX);

  float volts = getGlobalValue(F_VOLTS);
  if(fabsf(volts - self->lastVolts) > VOLTAGE_THRESHOLD) {
    self->lastVolts = volts;
  }

  self->finalPWM = self->pwmValue * (12.0f / self->lastVolts);
  self->finalPWM = hal_constrain(self->finalPWM, (int32_t)VP37_PWM_MIN, (int32_t)(VP37_PWM_MAX));

  if(self->lastPWMval != self->finalPWM) {
    self->lastPWMval = self->finalPWM;
    valToPWM(PIO_VP37_RPM, self->finalPWM);
  }
}

void VP37_init(VP37Pump *self) {
  if(self->vp37Initialized) {
    return;
  }

  if(!waitForAdjustometerBaseline()) {
    derr("VP37 adjustometer baseline not ready, cannot initialize");
    return;
  }

  self->lastThrottle = -1;
  self->calibrationDone = false;
  self->desiredAdjustometer = -1;
  self->currentAdjustometerPosition = -1;
  self->adjCommLostSince = 0;
  self->pidErr = 0;
  self->pwmValue = VP37_PWM_MIN;
  self->voltageCorrection = 0;
  self->lastPWMval = -1;
  self->finalPWM = VP37_PWM_MIN;
  self->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  self->pidTf = VP37_PID_TF;
  self->cmdLen = 0;
  self->cmdBuf[0] = '\0';

  if(self->adjustController == NULL) {
    self->adjustController = hal_pid_controller_create();
  }

  // Try loading tuned PID values from EEPROM; fall back to defaults.
  if(!VP37_loadPIDFromEEPROM(self)) {
    hal_pid_controller_set_kp(self->adjustController, VP37_PID_KP);
    hal_pid_controller_set_ki(self->adjustController, VP37_PID_KI);
    hal_pid_controller_set_kd(self->adjustController, VP37_PID_KD);
  }
  hal_pid_controller_set_tf(self->adjustController, self->pidTf);
  hal_pid_controller_set_max_integral(self->adjustController, PID_MAX_INTEGRAL);

  valToPWM(PIO_VP37_ANGLE, 0);

  VP37_makeCalibration(self);
  VP37_updateAdjustometerPosition(self);
  self->desiredAdjustometer = -1;

  VP37_enableVP37(self, self->calibrationDone);

  self->vp37Initialized = true;
}

void VP37_enableVP37(VP37Pump *self, bool enable) {
  (void)self;
  pcf8574_write(PCF8574_O_VP37_ENABLE, enable);
  deb("vp37 enabled: %d", VP37_isVP37Enabled(self));
}

bool VP37_isVP37Enabled(VP37Pump *self) {
  (void)self;
  return pcf8574_read(PCF8574_O_VP37_ENABLE);
}

void VP37_setInjectionTiming(VP37Pump *self, int32_t angle) {
  (void)self;
  angle = hal_constrain(angle, 0, 100);
  valToPWM(PIO_VP37_ANGLE, hal_map(angle, 0, 100, TIMING_PWM_MIN, TIMING_PWM_MAX));
}

void VP37_setVP37Throttle(VP37Pump *self, float accel) {
  if(!self->calibrationDone) {
    derr_limited("VP37 calibration", "Calibration not done!");
    return;
  }

  accel = hal_constrain(accel, (float)VP37_ACCELERATION_MIN, (float)VP37_ACCELERATION_MAX);
  self->desiredAdjustometer = (int32_t)
      mapfloat(accel, VP37_ACCELERATION_MIN, VP37_ACCELERATION_MAX, self->VP37_ADJUST_MIN, self->VP37_ADJUST_MAX);
}

int32_t VP37_getMinVP37ThrottleValue(VP37Pump *self) {
  (void)self;
  return VP37_ACCELERATION_MIN;
}

int32_t VP37_getMaxVP37ThrottleValue(VP37Pump *self) {
  (void)self;
  return VP37_ACCELERATION_MAX;
}

void VP37_setVP37PID(VP37Pump *self, float kp, float ki, float kd, bool shouldTriggerReset) {
  hal_pid_controller_set_kp(self->adjustController, kp);
  hal_pid_controller_set_ki(self->adjustController, ki);
  hal_pid_controller_set_kd(self->adjustController, kd);

  if(shouldTriggerReset) {
    hal_pid_controller_reset(self->adjustController);
    self->lastPWMval = -1;
    self->finalPWM = VP37_PWM_MIN;
  }
}

void VP37_getVP37PIDValues(VP37Pump *self, float *kp, float *ki, float *kd) {
  if(kp != NULL) {
    *kp = hal_pid_controller_get_kp(self->adjustController);
  }
  if(ki != NULL) {
    *ki = hal_pid_controller_get_ki(self->adjustController);
  }
  if(kd != NULL) {
    *kd = hal_pid_controller_get_kd(self->adjustController);
  }
}

float VP37_getVP37PIDTimeUpdate(VP37Pump *self) {
  return self->pidTimeUpdate;
}

void VP37_process(VP37Pump *self) {
  if(self->vp37Initialized) {
    VP37_updateAdjustometerPosition(self);

    if(self->adjCommLostSince != 0 &&
       (hal_millis() - self->adjCommLostSince) >= (VP37_ADJ_COMM_CUTOFF_S * SECOND)) {
      self->vp37Initialized = false;
      VP37_enableVP37(self, false);
      derr("VP37 disabled: adjustometer comm lost for %u s", VP37_ADJ_COMM_CUTOFF_S);
      return;
    }

    // if((self->VP37_ADJUST_MAX <= 0 || self->VP37_ADJUST_MIDDLE <= 0 || self->VP37_ADJUST_MIN <= 0) &&
    //    self->currentAdjustometerPosition > MIN_ADJUSTOMETER_VAL) {
    //   VP37_makeCalibration(self);
    //   VP37_updateAdjustometerPosition(self);
    //   self->desiredAdjustometer = self->currentAdjustometerPosition;
    // }

    int32_t rpm = (int32_t)getGlobalValue(F_RPM);
    if(rpm > RPM_MAX_EVER) {
      self->vp37Initialized = false;
      VP37_enableVP37(self, false);
      derr("RPM was too high! (%d)", rpm);
      return;
    }

    float thr = (float)getThrottlePercentage();
    if(thr > self->lastThrottle || self->desiredAdjustometer < 0) {
      // Accelerating or first run: apply immediately
      self->lastThrottle = thr;
      VP37_setVP37Throttle(self, thr);
    } else if(thr < self->lastThrottle) {
      // Decelerating: ramp down smoothly
      self->lastThrottle -= VP37_THROTTLE_RAMP_DOWN_STEP;
      if(self->lastThrottle < thr) {
        self->lastThrottle = thr;
      }
      VP37_setVP37Throttle(self, self->lastThrottle);
    }

    VP37_throttleCycle(self);
  }
}

static uint32_t lastPeriodicLogMs = 0;

void VP37_showDebug(VP37Pump *self) {

  uint32_t now = hal_millis();
  if (now - lastPeriodicLogMs >= VP37_DEBUG_UPDATE) {
    lastPeriodicLogMs = now;

    deb("thr:%.1f des:%d adj:%d V:%.1f t:%.1fC pwm:%d err:%d %.2f/%.2f/%.2f min:%d max:%d",
      self->lastThrottle,
      self->desiredAdjustometer,
      self->currentAdjustometerPosition,
      getGlobalValue(F_VOLTS),
      getGlobalValue(F_FUEL_TEMP),
      self->finalPWM,
      self->pidErr,
      hal_pid_controller_get_kp(self->adjustController),
      hal_pid_controller_get_ki(self->adjustController),
      hal_pid_controller_get_kd(self->adjustController),
      self->VP37_ADJUST_MIN,
      self->VP37_ADJUST_MAX);
  }

  // ── Serial PID tuner: read commands from USB CDC ────────────────────
  while(hal_serial_available() > 0) {
    int ch = hal_serial_read();
    if(ch < 0) break;

    if(ch == '\n' || ch == '\r') {
      if(self->cmdLen > 0) {
        self->cmdBuf[self->cmdLen] = '\0';
        VP37_processSerialCommand(self, self->cmdBuf);
        self->cmdLen = 0;
      }
    } else if(self->cmdLen < VP37_CMD_BUF_SIZE - 1) {
      self->cmdBuf[self->cmdLen++] = (char)ch;
    }
  }
}

// ── EEPROM persistence for PID tuning ────────────────────────────────────────

bool VP37_savePIDToEEPROM(VP37Pump *self) {
  float kp, ki, kd;
  VP37_getVP37PIDValues(self, &kp, &ki, &kd);

  bool ok = true;
  ok = hal_kv_set_u32(VP37_KV_KEY_PID_KP, float_to_u32(kp)) && ok;
  ok = hal_kv_set_u32(VP37_KV_KEY_PID_KI, float_to_u32(ki)) && ok;
  ok = hal_kv_set_u32(VP37_KV_KEY_PID_KD, float_to_u32(kd)) && ok;
  ok = hal_kv_set_u32(VP37_KV_KEY_PID_TU, float_to_u32(self->pidTimeUpdate)) && ok;
  ok = hal_kv_set_u32(VP37_KV_KEY_PID_TF, float_to_u32(self->pidTf)) && ok;

  if(ok) {
    hal_eeprom_commit();
  }
  return ok;
}

bool VP37_loadPIDFromEEPROM(VP37Pump *self) {
  uint32_t raw;
  float kp, ki, kd, tu, tf;

  if(!hal_kv_get_u32(VP37_KV_KEY_PID_KP, &raw)) return false;
  kp = u32_to_float(raw);
  if(!hal_kv_get_u32(VP37_KV_KEY_PID_KI, &raw)) return false;
  ki = u32_to_float(raw);
  if(!hal_kv_get_u32(VP37_KV_KEY_PID_KD, &raw)) return false;
  kd = u32_to_float(raw);
  if(!hal_kv_get_u32(VP37_KV_KEY_PID_TU, &raw)) return false;
  tu = u32_to_float(raw);

  // TF key may not exist in older EEPROM images — use default.
  if(hal_kv_get_u32(VP37_KV_KEY_PID_TF, &raw)) {
    tf = u32_to_float(raw);
  } else {
    tf = VP37_PID_TF;
  }

  // Sanity check — reject obviously broken values.
  if(kp < 0.0f || kp > 100.0f) return false;
  if(ki < 0.0f || ki > 100.0f) return false;
  if(kd < 0.0f || kd > 100.0f) return false;
  if(tu < 1.0f || tu > 1000.0f) return false;
  if(tf < 0.0f || tf > 10.0f) return false;

  hal_pid_controller_set_kp(self->adjustController, kp);
  hal_pid_controller_set_ki(self->adjustController, ki);
  hal_pid_controller_set_kd(self->adjustController, kd);
  self->pidTimeUpdate = tu;
  self->pidTf = tf;

  deb("\033[33mVP37 PID loaded from EEPROM: Kp=%.4f Ki=%.4f Kd=%.4f TU=%.1f TF=%.4f\033[0m",
      kp, ki, kd, tu, tf);
  return true;
}

// ── Serial command parser for runtime PID tuning ─────────────────────────────

static void VP37_processSerialCommand(VP37Pump *self, const char *cmd) {
  float val;

  if(cmd[0] == '?' || cmd[0] == 'H' || cmd[0] == 'h') {
    float kp, ki, kd;
    VP37_getVP37PIDValues(self, &kp, &ki, &kd);
    deb("\033[33mPID: Kp=%.4f Ki=%.4f Kd=%.4f TU=%.1f TF=%.4f\033[0m",
        kp, ki, kd, self->pidTimeUpdate, self->pidTf);
    deb("\033[33mCAL: MIN=%d MID=%d MAX=%d\033[0m", self->VP37_ADJUST_MIN,
        self->VP37_ADJUST_MIDDLE, self->VP37_ADJUST_MAX);
    deb("\033[33mCMD: P<val> I<val> D<val> T<val> F<val> S(save) R(reset) ?(help)\033[0m");
    return;
  }

  if(cmd[0] == 'S' || cmd[0] == 's') {
    if(VP37_savePIDToEEPROM(self)) {
      deb("\033[33mPID saved to EEPROM\033[0m");
    } else {
      derr("PID EEPROM save FAILED");
    }
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
