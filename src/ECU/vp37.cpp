
#include "vp37.h"

static SmartTimers fuelTempTimer;
static SmartTimers voltageTimer;

void measureFuelTemp(void) {
  setGlobalValue(F_FUEL_TEMP, getVP37FuelTemperature());
}

void measureVoltage(void) {
  setGlobalValue(F_VOLTS, getSystemSupplyVoltage());
}

static int VP37Pump_getMaxAdjustometerPWMVal(VP37Pump *self) {
  (void)self;
  return hal_map(VP37_CALIBRATION_MAX_PERCENTAGE, 0, 100, 0, PWM_RESOLUTION);
}

static int VP37Pump_getAdjustometerStable(VP37Pump *self) {
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

    measureFuelTemp();
    measureVoltage();

    self->adjustController.setKp(VP37_PID_KP);
    self->adjustController.setKi(VP37_PID_KI);
    self->adjustController.setKd(VP37_PID_KD);
    self->adjustController.setMaxIntegral(PID_MAX_INTEGRAL);

    fuelTempTimer.begin(measureFuelTemp, VP37_FUEL_TEMP_UPDATE);
    voltageTimer.begin(measureVoltage, VP37_VOLTAGE_UPDATE);

    self->vp37Initialized = true;
  }
}

static int VP37Pump_makeCalibrationValue(VP37Pump *self) {
  hal_delay_ms(VP37_ADJUST_TIMER);
  watchdog_feed();
  int val = VP37Pump_getAdjustometerStable(self);
  hal_delay_ms(VP37_ADJUST_TIMER);
  watchdog_feed();
  return val;
}

static void VP37Pump_throttleCycle(VP37Pump *self) {
  float output;

  self->adjustController.updatePIDtime(VP37_PID_TIME_UPDATE);
  output = self->adjustController.updatePIDcontroller(self->desiredAdjustometer - getVP37Adjustometer());

  self->pwmValue = mapfloat(output, self->VP37_ADJUST_MIN, self->VP37_ADJUST_MAX, VP37_PWM_MIN, VP37_PWM_MAX);

  float volts = getGlobalValue(F_VOLTS);
  if (fabs(volts - self->lastVolts) > VOLTAGE_THRESHOLD) {
    self->lastVolts = volts;
  }

  self->finalPWM = self->pwmValue * (12.0f / self->lastVolts);
  self->finalPWM = hal_constrain(self->finalPWM, (int)VP37_PWM_MIN, (int)(VP37_PWM_MAX));

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

    fuelTempTimer.tick();
    voltageTimer.tick();

    int rpm = (int)getGlobalValue(F_RPM);
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
      getVP37Adjustometer(), getGlobalValue(F_VOLTS), getGlobalValue(F_FUEL_TEMP), (int)self->finalPWM,
      (int)self->voltageCorrection);
}
