
#include "vp37.h"

Timer throttleTimer;
static bool vp37Initialized = false;
static int lastThrottle = -1;
static bool calibrationDone = false;
static int desiredAdjustometer = -1;
static float pwmValue = VP37_PWM_MIN;
static float voltageCorrection = 0;
static int finalPWM = VP37_PWM_MIN;
static float lastVolts = 0.0;

static int VP37_ADJUST_MIN, VP37_ADJUST_MIDDLE, VP37_ADJUST_MAX, VP37_OPERATE_MAX;
static PIDController controller;

void initPIDcontroller(void) {
  controller.kp = PID_KP;
  controller.ki = PID_KI;
  controller.kd = PID_KD;
  controller.last_time = 0;
}

void updatePIDtime(PIDController *c) {
  float now = millis();
  c->dt = (now - c->last_time)/100.00;
  c->last_time = now;
}

float updatePIDcontroller(PIDController *c, float error) {
  float proportional = error;
  c->integral += error * c->dt;
  float derivative = (error - c->previous) / c->dt;
  c->previous = error;
  return (c->kp * proportional) + (c->ki * c->integral) + (c->kd * derivative);
}

bool measureFuelTemp(void *arg) {
  valueFields[F_FUEL_TEMP] = getVP37FuelTemperature();
  return true;
}

bool measureVoltage(void *arg) {
  valueFields[F_VOLTS] = getSystemSupplyVoltage();
  return true;
}

int getMaxAdjustometerPWMVal(void) {
  return map(VP37_CALIBRATION_MAX_PERCENTAGE, 0, 100, 0, PWM_RESOLUTION);
}

void initVP37(void) {
  if(!vp37Initialized) {
    throttleTimer = timer_create_default();
    desiredAdjustometer = -1;
    measureFuelTemp(NULL);
    measureVoltage(NULL);

    initPIDcontroller();

    throttleTimer.every(VP37_FUEL_TEMP_UPDATE, measureFuelTemp);
    throttleTimer.every(VP37_VOLTAGE_UPDATE, measureVoltage);

    vp37Initialized = true; 
  }
}

int makeCalibrationValue(void) {
  delay(VP37_ADJUST_TIMER);
  watchdog_update();
  int val = getVP37Adjustometer();
  delay(VP37_ADJUST_TIMER);
  watchdog_update();  
  return val;
}

float getCalibrationError(int from) {
  return (float)((float)from * PERCENTAGE_ERROR / 100.0f);  
}

bool isInRangeOf(float desired, float val) {
  return (val >= (desired - (getCalibrationError(desired) / 2.0)) &&
         val <= (desired + (getCalibrationError(desired) / 2.0)) );
}

void vp37Calibrate(void) {

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

  enableVP37(true);
}

void enableVP37(bool enable) {
  pcf8574_write(PCF8574_O_VP37_ENABLE, enable);
  deb("vp37 enabled: %d", isVP37Enabled()); 
}

bool isVP37Enabled(void) {
  return pcf8574_read(PCF8574_O_VP37_ENABLE);
}

void throttleCycle(void) {
  updatePIDtime(&controller);

  float error = desiredAdjustometer - getVP37Adjustometer();
  float output = updatePIDcontroller(&controller, error);

  float maxPWMValue = VP37_PWM_MIN * 2;
  pwmValue = mapfloat(output, VP37_ADJUST_MIN, VP37_ADJUST_MAX, VP37_PWM_MIN, maxPWMValue);
  pwmValue = constrain(pwmValue, VP37_PWM_MIN, maxPWMValue);

  float diff = 0.0;
  if(valueFields[F_VOLTS] != lastVolts) {
    diff = fabs(valueFields[F_VOLTS] - lastVolts);
    lastVolts = valueFields[F_VOLTS];
  }

  if(diff > VOLT_MIN_DIFF) {
    voltageCorrection = (valueFields[F_VOLTS] - 12.0) / VOLT_PER_PWM;
  }
  finalPWM = int(pwmValue - voltageCorrection);

  valToPWM(PIO_VP37_RPM, finalPWM);
}

void vp37Process(void) {
  if(vp37Initialized) {
    if((VP37_ADJUST_MAX <= 0 || 
      VP37_ADJUST_MIDDLE <= 0 || 
      VP37_ADJUST_MIN <= 0) && 
      getVP37Adjustometer() > MIN_ADJUSTOMETER_VAL) {
        vp37Calibrate();
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
  delayMicroseconds(VP37_OPERATION_DELAY);  
}

void showVP37Debug(void) {
  deb("thr:%d des:%d adj:%d V:%.1f t:%.1f pwm:%d %vc:%d", lastThrottle, desiredAdjustometer,
      getVP37Adjustometer(), valueFields[F_VOLTS], valueFields[F_FUEL_TEMP], (int)finalPWM,
      (int)voltageCorrection);
}
