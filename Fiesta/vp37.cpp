
#include "vp37.h"

Timer throttleTimer;

bool measureFuelTemp(void *arg) {
  valueFields[F_FUEL_TEMP] = getVP37FuelTemperature();
  return true;
}

bool measureVoltage(void *arg) {
  valueFields[F_VOLTS] = getSystemSupplyVoltage();
  return true;
}

VP37Pump::VP37Pump() { }

int VP37Pump::getMaxAdjustometerPWMVal(void) {
  return map(VP37_CALIBRATION_MAX_PERCENTAGE, 0, 100, 0, PWM_RESOLUTION);
}

int VP37Pump::getAdjustometerStable(void) {
  for(int a = 0; a < STABILITY_ADJUSTOMETER_TAB_SIZE; a++) {
    adjustStabilityTable[a] = getVP37Adjustometer();
  }
  return getAverageFrom(adjustStabilityTable, STABILITY_ADJUSTOMETER_TAB_SIZE);
}

void VP37Pump::initVP37(void) {
  if(!vp37Initialized) {

    vp37Initialized = false;
    lastThrottle = -1;
    calibrationDone = false;
    desiredAdjustometer = -1;
    pwmValue = VP37_PWM_MIN;
    voltageCorrection = 0;
    lastPWMval = -1;
    finalPWM = VP37_PWM_MIN;

    throttleTimer = timer_create_default();
    measureFuelTemp(NULL);
    measureVoltage(NULL);

    adjustController = new PIDController(VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, PID_MAX_INTEGRAL);

    throttleTimer.every(VP37_FUEL_TEMP_UPDATE, measureFuelTemp);
    throttleTimer.every(VP37_VOLTAGE_UPDATE, measureVoltage);

    vp37Initialized = true; 
  }
}

int VP37Pump::makeCalibrationValue(void) {
  m_delay(VP37_ADJUST_TIMER);
  watchdog_feed();
  int val = getAdjustometerStable();
  m_delay(VP37_ADJUST_TIMER);
  watchdog_feed();  
  return val;
}

float VP37Pump::getCalibrationError(int from) {
  return (float)((float)from * PERCENTAGE_ERROR / 100.0f);  
}

bool VP37Pump::isInRangeOf(float desired, float val) {
  return (val >= (desired - (getCalibrationError(desired) / 2.0)) &&
         val <= (desired + (getCalibrationError(desired) / 2.0)) );
}

void VP37Pump::init(void) {

  if(calibrationDone) {
    return;
  }
  initVP37();

  VP37_ADJUST_MAX = VP37_ADJUST_MIDDLE = VP37_ADJUST_MIN = VP37_OPERATE_MAX = -1;

  valToPWM(PIO_VP37_RPM, getMaxAdjustometerPWMVal());
  VP37_ADJUST_MAX = percentToGivenVal(VP37_PERCENTAGE_LIMITER, makeCalibrationValue());
  valToPWM(PIO_VP37_RPM, 0);
  VP37_ADJUST_MIN = makeCalibrationValue();
  VP37_ADJUST_MIDDLE = ((VP37_ADJUST_MAX - VP37_ADJUST_MIN) / 2) + VP37_ADJUST_MIN;
  calibrationDone = VP37_ADJUST_MIDDLE > 0;

  enableVP37(calibrationDone);
}

void VP37Pump::enableVP37(bool enable) {
  pcf8574_write(PCF8574_O_VP37_ENABLE, enable);
  deb("vp37 enabled: %d", isVP37Enabled()); 
}

bool VP37Pump::isVP37Enabled(void) {
  return pcf8574_read(PCF8574_O_VP37_ENABLE);
}

void VP37Pump::throttleCycle(void) {
  float output;

  adjustController->updatePIDtime(VP37_PID_TIME_UPDATE);
  output = adjustController->updatePIDcontroller(desiredAdjustometer - getVP37Adjustometer());

  pwmValue = mapfloat(output, VP37_ADJUST_MIN, VP37_ADJUST_MAX, VP37_PWM_MIN, VP37_PWM_MAX);
  
  if (fabs(valueFields[F_VOLTS] - lastVolts) > VOLTAGE_THRESHOLD) {
    lastVolts = valueFields[F_VOLTS];
  }

  finalPWM = pwmValue * (12.0 / lastVolts);  
  finalPWM = constrain(finalPWM, VP37_PWM_MIN, VP37_PWM_MAX);

  if(lastPWMval != finalPWM) {
    lastPWMval = finalPWM;
    valToPWM(PIO_VP37_RPM, finalPWM);
  }
}

void VP37Pump::process(void) {
  if(vp37Initialized) {
    if((VP37_ADJUST_MAX <= 0 || 
      VP37_ADJUST_MIDDLE <= 0 || 
      VP37_ADJUST_MIN <= 0) && 
      getVP37Adjustometer() > MIN_ADJUSTOMETER_VAL) {
        init();
        lastThrottle = desiredAdjustometer = -1;
    }

    throttleTimer.tick();  

    int rpm = int(valueFields[F_RPM]);
    if(rpm > RPM_MAX_EVER) {
      enableVP37(false);
      derr("RPM was too high! (%d)", rpm);
      return;
    }

    int thr = getThrottlePercentage();
    if(lastThrottle != thr || desiredAdjustometer < 0) {
      lastThrottle = thr;
      desiredAdjustometer = map(thr, 0, 100, VP37_ADJUST_MIN, VP37_ADJUST_MAX);
    }

    throttleCycle();
  }
}

void VP37Pump::showVP37Debug(void) {
  deb("thr:%d des:%d adj:%d V:%.1f t:%.1fC pwm:%d %vc:%d", lastThrottle, desiredAdjustometer,
      getVP37Adjustometer(), valueFields[F_VOLTS], valueFields[F_FUEL_TEMP], (int)finalPWM,
      (int)voltageCorrection);
}
