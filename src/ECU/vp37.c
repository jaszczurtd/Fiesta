#include "vp37.h"
#include <hal/hal_soft_timer.h>

static void VP37_makeCalibration(VP37Pump *self);
static void VP37_updateAdjustometerPosition(VP37Pump *self);

VP37InitStatus VP37_init(VP37Pump *self) {
  if(self->vp37Initialized) {
    return VP37_INIT_ALREADY_INITIALIZED;
  }

  if(!waitForAdjustometerBaseline()) {
    derr_limited("VP37 init baseline", "VP37 adjustometer baseline not ready, cannot initialize");
    return VP37_INIT_BASELINE_NOT_READY;
  }

  self->lastThrottle = -1;
  self->calibrationDone = false;
  self->desiredAdjustometerTarget = -1;
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
  self->throttleRampLastMs = hal_millis();

  if(self->adjustController == NULL) {
    self->adjustController = hal_pid_controller_create();
    if(self->adjustController == NULL) {
      derr("VP37 init failed: cannot create PID controller");
      return VP37_INIT_PID_CREATE_FAILED;
    }
  }

  hal_pid_controller_set_kp(self->adjustController, VP37_PID_KP);
  hal_pid_controller_set_ki(self->adjustController, VP37_PID_KI);
  hal_pid_controller_set_kd(self->adjustController, VP37_PID_KD);
  hal_pid_controller_set_tf(self->adjustController, self->pidTf);
  hal_pid_controller_set_max_integral(self->adjustController, VP37_PID_MAX_INTEGRAL);

  valToPWM(PIO_VP37_ANGLE, 0);

  VP37_makeCalibration(self);
  VP37_updateAdjustometerPosition(self);
  self->desiredAdjustometerTarget = -1;
  self->desiredAdjustometer = -1;

  VP37_enableVP37(self, self->calibrationDone);

  self->vp37Initialized = true;
  return VP37_INIT_OK;
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
    derr_limited("VP37 adj comm", "Failed to read adjustometer for VP37 control");
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
                                       -(float)VP37_PID_CORR_LIMIT,
                                       +(float)VP37_PID_CORR_LIMIT);
  deb("VP37 calibration: MIN=%d MIDDLE=%d MAX=%d OPERATE_MAX=%d", 
      self->VP37_ADJUST_MIN, 
      self->VP37_ADJUST_MIDDLE, 
      self->VP37_ADJUST_MAX, 
      self->VP37_OPERATE_MAX);
}

static void VP37_throttleCycle(VP37Pump *self) {
  if(self->desiredAdjustometerTarget < 0) {
    return;
  }

  hal_pid_controller_update_time(self->adjustController, self->pidTimeUpdate);

  // First-run snap: when no slewed setpoint exists yet, jump to target
  // immediately so PID has a sensible reference from the first cycle.
  if(self->desiredAdjustometer < 0) {
    self->desiredAdjustometer = self->desiredAdjustometerTarget;
  } else {
    // Slew-rate limit on the setpoint fed to PID. Prevents huge instantaneous
    // jumps in pwm_ff (and therefore PWM) when throttle changes abruptly,
    // which would otherwise slam the actuator into its mechanical stops.
    int32_t delta = self->desiredAdjustometerTarget - self->desiredAdjustometer;
    if(delta > VP37_DESIRED_SLEW_PER_CYCLE) {
      delta = VP37_DESIRED_SLEW_PER_CYCLE;
    } else if(delta < -VP37_DESIRED_SLEW_PER_CYCLE_DOWN) {
      delta = -VP37_DESIRED_SLEW_PER_CYCLE_DOWN;
    }
    self->desiredAdjustometer += delta;
  }

  // Clamp measured position to the calibrated range.  When the adjustometer
  // physically exceeds the calibrated max (mechanical saturation at the upper
  // end of travel), clamping prevents inflated error and integral windup
  // from a region the controller cannot influence.
  int32_t clampedAdj = hal_constrain(self->currentAdjustometerPosition,
                                     self->VP37_ADJUST_MIN, self->VP37_ADJUST_MAX);
  self->pidErr = self->desiredAdjustometer - clampedAdj;

  // Deadband: suppress small errors to prevent integral windup and let
  // the system settle when already close to the setpoint.
  float pidInput = (float)self->pidErr;
  if(pidInput > -VP37_PID_DEADBAND && pidInput < VP37_PID_DEADBAND) {
    pidInput = 0.0f;
  }

  // PID output is now a PWM correction (units: PWM counts), bounded to
  // +/- VP37_PID_CORR_LIMIT by set_output_limits().
  float pidCorrection = hal_pid_controller_update(self->adjustController, pidInput);

  // Feedforward: linear interpolation of expected steady-state PWM as a
  // function of the (slewed) desired position. This carries the bulk of the
  // control signal so PID only has to trim residual error.
  float pwmFF = mapfloat((float)self->desiredAdjustometer,
                         (float)self->VP37_ADJUST_MIN, (float)self->VP37_ADJUST_MAX,
                         (float)VP37_PWM_FF_AT_MIN, (float)VP37_PWM_FF_AT_MAX);

  self->pwmValue = pwmFF + pidCorrection;

  // Soft floor on the command (nominal-voltage units), BEFORE voltage
  // compensation. While ramping up to a new (higher) target the slewed
  // setpoint is below the target, so pwmFF and the resulting pwmValue
  // can be too low to actually drive the actuator there in reasonable
  // time. Compute the FF that would correspond to the final target and
  // ensure pwmValue stays at least (FF_target - margin). Only enforce
  // when the actuator is still below the target (climb phase); during
  // overshoot/recovery the controller must be free to lower PWM.
  if(self->desiredAdjustometerTarget > 0 &&
     self->currentAdjustometerPosition < self->desiredAdjustometerTarget) {
    float pwmFFTarget = mapfloat((float)self->desiredAdjustometerTarget,
                                 (float)self->VP37_ADJUST_MIN, (float)self->VP37_ADJUST_MAX,
                                 (float)VP37_PWM_FF_AT_MIN, (float)VP37_PWM_FF_AT_MAX);
    float softFloor = pwmFFTarget - (float)VP37_PWM_FF_SOFT_FLOOR_MARGIN;
    if(self->pwmValue < softFloor) {
      self->pwmValue = softFloor;
    }
  }

  // Voltage compensation: scale PWM inversely with battery voltage so the
  // average coil current (and therefore actuator force) stays consistent.
  // Clamp Vbat to a minimum so a noisy/missing sensor cannot inflate PWM.
  float volts = getGlobalValue(F_VOLTS);
  if(volts < VP37_MIN_COMPENSATION_VOLTAGE) {
    volts = VP37_MIN_COMPENSATION_VOLTAGE;
  }
  self->lastVolts = volts;
  self->finalPWM = self->pwmValue * (NOMINAL_VOLTAGE / self->lastVolts);

  self->finalPWM = hal_constrain(self->finalPWM, (int32_t)VP37_PWM_MIN, (int32_t)(VP37_PWM_MAX));

  if(self->lastPWMval != self->finalPWM) {
    self->lastPWMval = self->finalPWM;
    valToPWM(PIO_VP37_RPM, self->finalPWM);
  }
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

  accel = mapfloat(accel, 
    (float)VP37_PERCENT_MIN, 
    (float)VP37_PERCENT_MAX, 
    (float)VP37_ACCELERATION_MIN, (float)VP37_ACCELERATION_MAX);

  accel = hal_constrain(accel, 
    (float)VP37_PERCENT_MIN, 
    (float)VP37_PERCENT_MAX);
  self->desiredAdjustometerTarget = (int32_t)
      mapfloat(accel, 
        VP37_PERCENT_MIN, 
        VP37_PERCENT_MAX, 
        (float)self->VP37_ADJUST_MIN, 
        (float)self->VP37_ADJUST_MAX);
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
    if(thr > self->lastThrottle || self->desiredAdjustometerTarget < 0) {
      // Accelerating or first run: apply immediately
      self->lastThrottle = thr;
      self->throttleRampLastMs = hal_millis();
      VP37_setVP37Throttle(self, thr);
    } else if(thr < self->lastThrottle) {
      // Decelerating: ramp down in time-based steps for predictable behavior.
      uint32_t nowMs = hal_millis();
      uint32_t elapsedMs = nowMs - self->throttleRampLastMs;
      if(elapsedMs >= VP37_THROTTLE_RAMP_DOWN_INTERVAL_MS) {
        float steps = (float)elapsedMs / (float)VP37_THROTTLE_RAMP_DOWN_INTERVAL_MS;
        self->lastThrottle -= (VP37_THROTTLE_RAMP_DOWN_STEP * steps);
        if(self->lastThrottle < thr) {
          self->lastThrottle = thr;
        }
        self->throttleRampLastMs = nowMs;
        VP37_setVP37Throttle(self, self->lastThrottle);
      }
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

}

