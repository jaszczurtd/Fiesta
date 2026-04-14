#include "vp37.h"
#include <hal/hal_soft_timer.h>

void measureFuelTemp(void) {
  setGlobalValue(F_FUEL_TEMP, getVP37FuelTemperature());
}

void measureVoltage(void) {
  setGlobalValue(F_VOLTS, getSystemSupplyVoltage());
}

static int32_t VP37_getMaxAdjustometerPWMVal(VP37Pump *self) {
  (void)self;
  return hal_map(VP37_CALIBRATION_MAX_PERCENTAGE, 0, 100, 0, PWM_RESOLUTION);
}

static int32_t VP37_getAdjustometerStable(VP37Pump *self) {
  int32_t count = STABILITY_ADJUSTOMETER_TAB_SIZE;
  if(count <= 0) {
    return getVP37Adjustometer();
  }

  int32_t firstValue = getVP37Adjustometer();
  int32_t sum = firstValue;
  int32_t minVal = firstValue;
  int32_t maxVal = firstValue;
  self->adjustStabilityTable[0] = firstValue;

  for(int32_t a = 1; a < count; a++) {
    int32_t value = getVP37Adjustometer();
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
  self->currentAdjustometerPosition = getVP37Adjustometer();
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
  self->calibrationDone = self->VP37_ADJUST_MIDDLE > 0;

  hal_pid_controller_set_output_limits(self->adjustController,
                                       (float)self->VP37_ADJUST_MIN,
                                       (float)self->VP37_ADJUST_MAX);
}

static void VP37_initVP37(VP37Pump *self) {
  if(!self->vp37Initialized) {

    self->lastThrottle = -1;
    self->calibrationDone = false;
    self->desiredAdjustometer = -1;
    self->currentAdjustometerPosition = -1;
    self->pidErr = 0;
    self->pwmValue = VP37_PWM_MIN;
    self->voltageCorrection = 0;
    self->lastPWMval = -1;
    self->finalPWM = VP37_PWM_MIN;
    self->fuelTempTimer = NULL;
    self->voltageTimer = NULL;

    measureFuelTemp();
    measureVoltage();

    if(self->adjustController == NULL) {
      self->adjustController = hal_pid_controller_create();
    }
    hal_pid_controller_set_kp(self->adjustController, VP37_PID_KP);
    hal_pid_controller_set_ki(self->adjustController, VP37_PID_KI);
    hal_pid_controller_set_kd(self->adjustController, VP37_PID_KD);
    hal_pid_controller_set_tf(self->adjustController, VP37_PID_TF);
    hal_pid_controller_set_max_integral(self->adjustController, PID_MAX_INTEGRAL);

    if(self->fuelTempTimer == NULL) {
      self->fuelTempTimer = hal_soft_timer_create();
    }
    if(self->voltageTimer == NULL) {
      self->voltageTimer = hal_soft_timer_create();
    }
    (void)hal_soft_timer_begin(self->fuelTempTimer, measureFuelTemp, VP37_FUEL_TEMP_UPDATE);
    (void)hal_soft_timer_begin(self->voltageTimer, measureVoltage, VP37_VOLTAGE_UPDATE);

    self->vp37Initialized = true;
  }
}

static void VP37_throttleCycle(VP37Pump *self) {
  if(self->desiredAdjustometer < 0) {
    return;
  }

  hal_pid_controller_update_time(self->adjustController, VP37_PID_TIME_UPDATE);

  VP37_updateAdjustometerPosition(self);
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

  VP37_initVP37(self);

  valToPWM(PIO_VP37_ANGLE, 0);

  VP37_makeCalibration(self);
  VP37_updateAdjustometerPosition(self);
  self->desiredAdjustometer = -1;

  VP37_enableVP37(self, self->calibrationDone);
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
    derr("Calibration not done!");
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
  (void)self;
  return VP37_PID_TIME_UPDATE;
}

void VP37_process(VP37Pump *self) {
  if(self->vp37Initialized) {
    if(self->currentAdjustometerPosition < MIN_ADJUSTOMETER_VAL) {
      VP37_updateAdjustometerPosition(self);
    }

    if((self->VP37_ADJUST_MAX <= 0 || self->VP37_ADJUST_MIDDLE <= 0 || self->VP37_ADJUST_MIN <= 0) &&
       self->currentAdjustometerPosition > MIN_ADJUSTOMETER_VAL) {
      VP37_makeCalibration(self);
      VP37_updateAdjustometerPosition(self);
      self->desiredAdjustometer = self->currentAdjustometerPosition;
    }

    hal_soft_timer_tick(self->fuelTempTimer);
    hal_soft_timer_tick(self->voltageTimer);

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

void VP37_showDebug(VP37Pump *self) {
  deb("thr:%.1f des:%d adj:%d V:%.1f t:%.1fC pwm:%d err:%d %.2f/%.2f/%.2f",
      self->lastThrottle,
      self->desiredAdjustometer,
      self->currentAdjustometerPosition,
      getGlobalValue(F_VOLTS),
      getGlobalValue(F_FUEL_TEMP),
      self->finalPWM,
      self->pidErr,
      hal_pid_controller_get_kp(self->adjustController),
      hal_pid_controller_get_ki(self->adjustController),
      hal_pid_controller_get_kd(self->adjustController));
}
