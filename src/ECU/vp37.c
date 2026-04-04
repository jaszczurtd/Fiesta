
#include "vp37.h"
#include <hal/hal_soft_timer.h>

void measureFuelTemp(void) {
  setGlobalValue(F_FUEL_TEMP, getVP37FuelTemperature());
}

void measureVoltage(void) {
  setGlobalValue(F_VOLTS, getSystemSupplyVoltage());
}

static int32_t VP37Pump_getMaxAdjustometerPWMVal(VP37Pump *self) {
  (void)self;
  return hal_map(VP37_CALIBRATION_MAX_PERCENTAGE, 0, 100, 0, PWM_RESOLUTION);
}

static int32_t VP37Pump_getAdjustometerStable(VP37Pump *self) {
  for(int a = 0; a < STABILITY_ADJUSTOMETER_TAB_SIZE; a++) {
    self->adjustStabilityTable[a] = getVP37Adjustometer();
  }
  return getAverageFrom(self->adjustStabilityTable, STABILITY_ADJUSTOMETER_TAB_SIZE);
}

static void VP37Pump_initVP37(VP37Pump *self) {
  if(!self->vp37Initialized) {

    self->lastThrottle = -1;
    self->calibrationDone = false;
    self->desiredAdjustometer = -1;
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

static int32_t VP37Pump_makeCalibrationValue(VP37Pump *self) {
  hal_delay_ms(VP37_ADJUST_TIMER);
  watchdog_feed();
  int val = VP37Pump_getAdjustometerStable(self);
  hal_delay_ms(VP37_ADJUST_TIMER);
  watchdog_feed();
  return val;
}

static void VP37Pump_throttleCycle(VP37Pump *self) {
  float output;

  hal_pid_controller_update_time(self->adjustController, VP37_PID_TIME_UPDATE);
  output = hal_pid_controller_update(self->adjustController,
                                     (float)(self->desiredAdjustometer - getVP37Adjustometer()));

  self->pwmValue = mapfloat(output, self->VP37_ADJUST_MIN, self->VP37_ADJUST_MAX, VP37_PWM_MIN, VP37_PWM_MAX);

  float volts = getGlobalValue(F_VOLTS);
  if (fabs(volts - self->lastVolts) > VOLTAGE_THRESHOLD) {
    self->lastVolts = volts;
  }

  self->finalPWM = self->pwmValue * (12.0f / self->lastVolts);
  self->finalPWM = hal_constrain(self->finalPWM, (int32_t)VP37_PWM_MIN, (int32_t)(VP37_PWM_MAX));

  if(self->lastPWMval != self->finalPWM) {
    self->lastPWMval = self->finalPWM;
    valToPWM(PIO_VP37_RPM, self->finalPWM);
  }
}

void VP37Pump_init(VP37Pump *self) {

  if(self->calibrationDone) {
    return;
  }
  VP37Pump_initVP37(self);

  self->VP37_ADJUST_MAX = self->VP37_ADJUST_MIDDLE = self->VP37_ADJUST_MIN = self->VP37_OPERATE_MAX = -1;

  valToPWM(PIO_VP37_RPM, VP37Pump_getMaxAdjustometerPWMVal(self));
  self->VP37_ADJUST_MAX = percentToGivenVal(VP37_PERCENTAGE_LIMITER, VP37Pump_makeCalibrationValue(self));
  valToPWM(PIO_VP37_RPM, 0);
  self->VP37_ADJUST_MIN = VP37Pump_makeCalibrationValue(self);
  self->VP37_ADJUST_MIDDLE = ((self->VP37_ADJUST_MAX - self->VP37_ADJUST_MIN) / 2) + self->VP37_ADJUST_MIN;
  self->calibrationDone = self->VP37_ADJUST_MIDDLE > 0;

  VP37Pump_enableVP37(self, self->calibrationDone);
}

void VP37Pump_enableVP37(VP37Pump *self, bool enable) {
  (void)self;
  pcf8574_write(PCF8574_O_VP37_ENABLE, enable);
  deb("vp37 enabled: %d", VP37Pump_isVP37Enabled(self));
}

bool VP37Pump_isVP37Enabled(VP37Pump *self) {
  (void)self;
  return pcf8574_read(PCF8574_O_VP37_ENABLE);
}

void VP37Pump_process(VP37Pump *self) {
  if(self->vp37Initialized) {
    if((self->VP37_ADJUST_MAX <= 0 ||
      self->VP37_ADJUST_MIDDLE <= 0 ||
      self->VP37_ADJUST_MIN <= 0) &&
      getVP37Adjustometer() > MIN_ADJUSTOMETER_VAL) {
        VP37Pump_init(self);
        self->lastThrottle = self->desiredAdjustometer = -1;
    }

    hal_soft_timer_tick(self->fuelTempTimer);
    hal_soft_timer_tick(self->voltageTimer);

    int32_t rpm = (int32_t)getGlobalValue(F_RPM);
    if(rpm > RPM_MAX_EVER) {
      VP37Pump_enableVP37(self, false);
      derr("RPM was too high! (%d)", rpm);
      return;
    }

    int thr = getThrottlePercentage();
    if(self->lastThrottle != thr || self->desiredAdjustometer < 0) {
      self->lastThrottle = thr;
      self->desiredAdjustometer = hal_map(thr, 0, 100, self->VP37_ADJUST_MIN, self->VP37_ADJUST_MAX);
    }

    VP37Pump_throttleCycle(self);
  }
}

void VP37Pump_showDebug(VP37Pump *self) {
  deb("thr:%d des:%d adj:%d V:%.1f t:%.1fC pwm:%d %vc:%d", self->lastThrottle, self->desiredAdjustometer,
      getVP37Adjustometer(), getGlobalValue(F_VOLTS), getGlobalValue(F_FUEL_TEMP), self->finalPWM,
      (int32_t)self->voltageCorrection);
}
