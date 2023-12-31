
#include "vp37.h"

#define THROTTLE_DELAY 500

Timer throttleTimer;
static bool vp37Initialized = false;
static volatile unsigned int adjustAngleCounter = 0;
static volatile unsigned long adjTime = 0;
static int adjustometerRAWval = 0;
static int currentVP37PWM = 0;
static int lastThrottle = -1;
static int lastDesired = -1;
static int lastDirection = D_NONE;
static int pwmval;

static int VP37_ADJUST_MIN, VP37_ADJUST_MIDDLE, VP37_ADJUST_MAX;
static int calibrationTab[VP37_CALIBRATION_CYCLES];

bool throttleCycle(void *arg);
void adjustometerInterrupt(void);

void initVP37(void) {
  if(!vp37Initialized) {
    adjustAngleCounter = 0;
    adjTime = 0;
    currentVP37PWM = 0;
    throttleTimer = timer_create_default();
    throttleTimer.every(THROTTLE_DELAY, throttleCycle);
    pwmval = 0;
    valToPWM(PIO_VP37_RPM, pwmval);

    pinMode(PIO_VP37_ADJUSTOMETER, INPUT_PULLUP); 
    attachInterrupt(PIO_VP37_ADJUSTOMETER, adjustometerInterrupt, FALLING);
    vp37Initialized = true; 
  }
}

int getAdjustometerVal(void) {
  int val = ((adjustometerRAWval / 10) * 10);

  val = map(val, VP37_ADJUST_MIN, VP37_ADJUST_MAX, 0, PWM_RESOLUTION);
  if(val < 0) {
    val = 0;
  }
  if(val > PWM_RESOLUTION) {
    val = PWM_RESOLUTION;
  }
  return val;
}

void makeCalibrationTable(void) {
  watchdog_update();
  delay(VP37_ADJUST_TIMER);
  for(int a = 0; a < VP37_CALIBRATION_CYCLES; a++) {
    calibrationTab[a] = adjustometerRAWval;
    delay(VP37_ADJUST_TIMER);
  }
  delay(VP37_ADJUST_TIMER);
  watchdog_update();  
}

int getCalibrationError(int from) {
  return (int)((float)from * PERCENTAGE_ERROR / 100.0f);  
}

bool isInRange(int pos) {
  int v = getAdjustometerVal();
  return (v >= (pos - (getCalibrationError(pos) / 2)) &&
          v <= (pos + (getCalibrationError(pos) / 2)) );
}

void vp37Calibrate(void) {
  initVP37();

  VP37_ADJUST_MAX = VP37_ADJUST_MIDDLE = VP37_ADJUST_MIN = 0;

  valToPWM(PIO_VP37_RPM, map(VP37_CALIBRATION_MAX_PERCENTAGE, 0, 100, 0, PWM_RESOLUTION));
  makeCalibrationTable();
  VP37_ADJUST_MAX = getAverageFrom(calibrationTab, VP37_CALIBRATION_CYCLES);
  valToPWM(PIO_VP37_RPM, 0);
  makeCalibrationTable();
  VP37_ADJUST_MIN = getAverageFrom(calibrationTab, VP37_CALIBRATION_CYCLES);
  VP37_ADJUST_MIDDLE = ((VP37_ADJUST_MIN - VP37_ADJUST_MAX) / 2) + VP37_ADJUST_MAX;
  enableVP37(true);
}

void adjustometerInterrupt(void) {
  detachInterrupt(PIO_VP37_ADJUSTOMETER);

  unsigned long currentMillis = millis();
  if (adjTime <= currentMillis) {
    adjTime = currentMillis + VP37_ADJUST_TIMER;
    adjustometerRAWval = adjustAngleCounter;
    adjustAngleCounter = 0;
  } else {
    adjustAngleCounter++;
  }

  attachInterrupt(PIO_VP37_ADJUSTOMETER, adjustometerInterrupt, FALLING);
}

void enableVP37(bool enable) {
  pcf8574_write(PCF8574_O_VP37_ENABLE, enable);
  deb("vp37 enabled: %d", isVP37Enabled()); 
}

bool isVP37Enabled(void) {
  return pcf8574_read(PCF8574_O_VP37_ENABLE);
}

int percentToPWMVal(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  return VP37_PWM_MIN + (((VP37_PWM_MAX - VP37_PWM_MIN) * percent) / 100);
}

void vp37Process(void) {
  if(!vp37Initialized) {
    return;
  }
  throttleTimer.tick();  

  int rpm = int(valueFields[F_RPM]);
  if(rpm > RPM_MAX_EVER) {
    enableVP37(false);
    derr("RPM was too high! (%d)", rpm);
    return;
  }

  int thr = getThrottlePercentage();
  int val = map(thr, 0, 100, VP37_PWM_MIN, VP37_PWM_MAX);
  int desired = map(map(thr, 0, 100, VP37_ADJUST_MIN, VP37_ADJUST_MAX), 
                    VP37_ADJUST_MIN, VP37_ADJUST_MAX, 0, PWM_RESOLUTION);

  if(lastThrottle != val || lastDesired != desired) {
    lastDirection = (lastDesired < desired) ? D_ADD : D_SUB;
    lastThrottle = val;
    lastDesired = desired;

    //valToPWM(PIO_VP37_RPM, lastThrottle + pwmval);

    deb("thr:%d val:%d adj:%d desired:%d dir:%d", thr, val, getAdjustometerVal(), desired, lastDirection);
  }
}

int ppp = 0;
bool dir = true;
bool throttleCycle(void *arg) {
  bool status = true;

  int val = map(ppp, 0, 100, VP37_PWM_MIN, VP37_PWM_MAX);
  valToPWM(PIO_VP37_RPM, val);

  if(dir) {
    ppp += 5;
    if(ppp > 100) {
      dir = false;
    }
  }
  if(!dir) {
    ppp-= 5;
    if(ppp < 0) {
      dir = true;
    }
  }

  return status;
}

