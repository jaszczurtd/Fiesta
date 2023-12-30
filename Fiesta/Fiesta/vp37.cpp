
#include "vp37.h"

#define THROTTLE_DELAY 2

Timer throttleTimer;
static bool vp37Initialized = false;
static volatile unsigned int adjustAngleCounter = 0;
static volatile unsigned long adjTime = 0;
static int adjustometerVal = 0;
static int adjustometerRAWval = 0;
static int currentVP37PWM = 0;
static int lastThrottle = -1;

static int VP37_ADJUST_MIN;
static int VP37_ADJUST_MAX;
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

    pinMode(PIO_VP37_ADJUSTOMETER, INPUT_PULLUP); 
    attachInterrupt(PIO_VP37_ADJUSTOMETER, adjustometerInterrupt, FALLING);
    vp37Initialized = true; 
  }
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

void vp37Calibrate(void) {
  initVP37();

  VP37_ADJUST_MAX = VP37_ADJUST_MIN = 0;

  valToPWM(PIO_VP37_RPM, 0);
  makeCalibrationTable();
  VP37_ADJUST_MIN = getMinFrom(calibrationTab, VP37_CALIBRATION_CYCLES);
  valToPWM(PIO_VP37_RPM, map(VP37_CALIBRATION_MAX_PERCENTAGE, 0, 100, 0, PWM_RESOLUTION));
  makeCalibrationTable();
  VP37_ADJUST_MAX = getMinFrom(calibrationTab, VP37_CALIBRATION_CYCLES);

  valToPWM(PIO_VP37_RPM, 0);


  enableVP37(true);
}

void adjustometerInterrupt(void) {
  detachInterrupt(PIO_VP37_ADJUSTOMETER);

  unsigned long currentMillis = millis();
  if (adjTime <= currentMillis) {
    adjTime = currentMillis + VP37_ADJUST_TIMER;
    adjustometerRAWval = ((adjustAngleCounter / 10) * 10);
    adjustAngleCounter = 0;

    int val = map(adjustometerRAWval, VP37_ADJUST_MIN, VP37_ADJUST_MAX,
                           0, PWM_RESOLUTION);
    if(val < 0) {
      val = 0;
    }
    if(val > PWM_RESOLUTION) {
      val = PWM_RESOLUTION;
    }
    adjustometerVal = val;

  } else {
    adjustAngleCounter++;
  }

  attachInterrupt(PIO_VP37_ADJUSTOMETER, adjustometerInterrupt, FALLING);
}

bool throttleCycle(void *arg) {

  bool status = true;

  
  if(lastThrottle > adjustometerVal) {
    currentVP37PWM += 1;
    if(currentVP37PWM > PWM_RESOLUTION) {
      currentVP37PWM = PWM_RESOLUTION;
    }
  }

  if(lastThrottle < adjustometerVal) {
    currentVP37PWM -= 1;
    if(currentVP37PWM < 0) {
      currentVP37PWM = 0;
    }
  }

  //valToPWM(PIO_VP37_RPM, currentVP37PWM);
  return status;
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
  if(lastThrottle != val) {
    lastThrottle = val;

    //throttleTimer.cancel();
    //throttleTimer.every(THROTTLE_DELAY, throttleCycle);

    //valToPWM(PIO_VP37_RPM, lastThrottle);

  }


 

  deb("adjustometerVal:%d %d / %d %d", thr, val, adjustometerRAWval, adjustometerVal);
}

void idleTask(void) {
  // delay(CORE_OPERATION_DELAY);  
}