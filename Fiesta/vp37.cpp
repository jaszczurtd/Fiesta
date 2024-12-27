
#include "vp37.h"
#include "vp37_defs.h"

static Timer vp37MainTimer;

bool measureFuelTemp(void *arg) {
  valueFields[F_FUEL_TEMP] = getVP37FuelTemperature();
  return true;
}

bool measureVoltage(void *arg) {
  valueFields[F_VOLTS] = getSystemSupplyVoltage();
  return true;
}

VP37Pump::VP37Pump() { }

VP37Pump::~VP37Pump() {
  if(adjustController) {
    delete adjustController;
    adjustController = nullptr;
  }
}

int VP37Pump::getMaxAdjustometerPWMVal(void) {
  return map(VP37_CALIBRATION_MAX_PERCENTAGE, 0, 100, 0, PWM_RESOLUTION);
}

int VP37Pump::getAdjustometerStable(void) {
  int sum = 0;
  int minVal = INT_MAX; 
  int maxVal = INT_MIN; 
  int count = STABILITY_ADJUSTOMETER_TAB_SIZE;

  for (int i = 0; i < count; i++) {
    int value = getVP37Adjustometer();
    sum += value;

    if (value < minVal) {
      minVal = value;
    }
    if (value > maxVal) {
      maxVal = value;
    }
  }

  sum -= minVal;
  sum -= maxVal;

  return sum / (count - 2); 
}

void VP37Pump::applyDelay(void) {
  m_delay(VP37_ADJUST_TIMER);
  watchdog_feed();
}

int VP37Pump::makeCalibrationValue(void) {
  //delay is not an issue here
  applyDelay();
  int val = getAdjustometerStable();
  applyDelay();
  return val;
}

void VP37Pump::init(void) {

  if(vp37Initialized) {
    return;
  }

  valToPWM(PIO_VP37_ANGLE, 0);

  calibrationDone = false;
  lastPWMval = -1;
  finalPWM = VP37_PWM_MIN;

  if(!calibrationDone) {
    makeVP37Calibration();
  }

  adjustController = new PIDController(VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, MAX_INTEGRAL);
  adjustController->setOutputLimits(vp37AdjustMin, vp37AdjustMax);
  adjustController->setTf(VP37_PID_TF);

  vp37MainTimer = timer_create_default();
  measureFuelTemp(NULL);
  measureVoltage(NULL);

  vp37MainTimer.every(VP37_FUEL_TEMP_UPDATE, measureFuelTemp);
  vp37MainTimer.every(VP37_VOLTAGE_UPDATE, measureVoltage);
  updateVP37AdjustometerPosition();
  desiredAdjustometer = -1;

  vp37Initialized = true; 

  enableVP37(calibrationDone);
}

void VP37Pump::makeVP37Calibration(void) {
  vp37AdjustMax = vp37AdjustMiddle = vp37AdjustMin = -1;

  valToPWM(PIO_VP37_RPM, getMaxAdjustometerPWMVal());
  vp37AdjustMax = makeCalibrationValue();
  valToPWM(PIO_VP37_RPM, 0);
  vp37AdjustMin = makeCalibrationValue();
  vp37AdjustMiddle = ((vp37AdjustMax - vp37AdjustMin) / 2) + vp37AdjustMin;
  calibrationDone = vp37AdjustMiddle > 0;
}

void VP37Pump::enableVP37(bool enable) {
  pcf8574_write(PCF8574_O_VP37_ENABLE, enable);
  deb("vp37 enabled: %d", isVP37Enabled()); 
}

bool VP37Pump::isVP37Enabled(void) {
  return pcf8574_read(PCF8574_O_VP37_ENABLE);
}

void VP37Pump::VP37TickMainTimer(void) {
  if(vp37Initialized) {
    vp37MainTimer.tick();  
  }
}

void VP37Pump::setInjectionTiming(int angle) {
  angle = constrain(angle, 0, 100);
  valToPWM(PIO_VP37_ANGLE, 
    map(angle, 0, 100, TIMING_PWM_MIN, TIMING_PWM_MAX));
}

void VP37Pump::updateVP37AdjustometerPosition(void) {
  currentAdjustometerPosition = getVP37Adjustometer();  
}

void VP37Pump::process(void) {
  if (vp37Initialized) {
    if(currentAdjustometerPosition < MIN_ADJUSTOMETER_VAL) {
      updateVP37AdjustometerPosition();
    }

    if ((vp37AdjustMax <= 0 || 
         vp37AdjustMiddle <= 0 || 
         vp37AdjustMin <= 0) && 
         currentAdjustometerPosition > MIN_ADJUSTOMETER_VAL) {
      makeVP37Calibration();
      updateVP37AdjustometerPosition();
      desiredAdjustometer = currentAdjustometerPosition;
    }

    int rpm = int(valueFields[F_RPM]);
    if (rpm > RPM_MAX_EVER) {
      vp37Initialized = false;
      enableVP37(false);
      derr("RPM was too high! (%d)", rpm);
      return;
    }

    adjustController->updatePIDtime(VP37_PID_TIME_UPDATE);

    updateVP37AdjustometerPosition();
    pidErr = desiredAdjustometer - currentAdjustometerPosition;

    float pidOutput = adjustController->updatePIDcontroller(pidErr);
    float normalized = (pidOutput - vp37AdjustMin) / (vp37AdjustMax - vp37AdjustMin);
    finalPWM = VP37_PWM_MIN + normalized * (VP37_PWM_MAX - VP37_PWM_MIN);
    finalPWM = constrain(finalPWM, VP37_PWM_MIN, VP37_PWM_MAX);

    if (lastPWMval != finalPWM) {
      lastPWMval = finalPWM;
      valToPWM(PIO_VP37_RPM, finalPWM);
    }
  }
}

void VP37Pump::setVP37Throttle(int accel) {
  if(!calibrationDone) {
    derr("Calibration not done!");
    return;
  }
  accel = constrain(accel, VP37_ACCELERATION_MIN, VP37_ACCELERATION_MAX);

  desiredAdjustometer = map(accel, 
                            VP37_ACCELERATION_MIN, VP37_ACCELERATION_MAX, 
                            vp37AdjustMin, vp37AdjustMax);
}

int VP37Pump::getMinVP37ThrottleValue(void) {
  return VP37_ACCELERATION_MIN;
}
int VP37Pump::getMaxVP37ThrottleValue(void) {
  return VP37_ACCELERATION_MAX;
}

void VP37Pump::setVP37PID(float kp, float ki, float kd, bool shouldTriggerReset) {
  if(adjustController != nullptr) {
    adjustController->setKp(kp);
    adjustController->setKi(ki);
    adjustController->setKd(kd);
    if(shouldTriggerReset) {
      adjustController->reset();
      lastPWMval = -1;
      finalPWM = VP37_PWM_MIN;
    }
  }
}

void VP37Pump::getVP37PIDValues(float *kp, float *ki, float *kd) {
  if(adjustController != nullptr) {
    *kp = adjustController->getKp();
    *ki = adjustController->getKi();
    *kd = adjustController->getKd();
  }
}

int VP37Pump::getVP37PIDTimeUpdate(void) {
  return VP37_PID_TIME_UPDATE;
}

void VP37Pump::showDebug(void) {

#ifdef DEBUG_NORMAL
  deb("max:%d des:%d adj:%d V:%.1f t:%.1fC pwm:%d %d %.2f/%.2f/%.2f", getVP37AdjustMax(), 
      desiredAdjustometer, currentAdjustometerPosition, valueFields[F_VOLTS], valueFields[F_FUEL_TEMP], 
      (int)finalPWM, pidErr,
      adjustController->getKp(), adjustController->getKi(), adjustController->getKd());
#endif

  deb("%d %d %d %d %d %d %f %f %f", finalPWM,
                              vp37AdjustMax, desiredAdjustometer, currentAdjustometerPosition, pidErr,
                              (int)valueFields[F_FUEL_TEMP],
                              adjustController->getKp(), adjustController->getKi(), adjustController->getKd());

}
