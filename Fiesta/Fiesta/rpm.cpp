#include "rpm.h"

Timer rpmTimer;

#define CRANK_REVOLUTIONS 32.0

static volatile unsigned long previousMillis = 0;
static volatile unsigned long shortPulse = 0;
static volatile unsigned long lastPulse = 0;
static volatile long rpmAliveTime = 0;
static volatile int RPMpulses = 0;

static int currentRPMSolenoid = 0;
static bool rpmCycle = false;
static int rpmPercentValue = 0;

void countRPM(void) {

  unsigned long _micros = micros();
  unsigned long nowPulse = _micros - lastPulse;
  
  lastPulse = _micros;

  if((nowPulse >> 1) > shortPulse){ 
    RPMpulses++;
    shortPulse = nowPulse; 
  } else { 
    shortPulse = nowPulse;
  }

  unsigned long _millis = millis();
  rpmAliveTime = _millis + RESET_RPM_WATCHDOG_TIME;
  if(_millis - previousMillis >= RPM_REFRESH_INTERVAL) {
    previousMillis = _millis;

    int RPM = int((RPMpulses * (MILIS_IN_MINUTE / float(RPM_REFRESH_INTERVAL))) / CRANK_REVOLUTIONS) - RPM_CORRECTION_VAL; 
    if(RPM < 0) {
      RPM = 0;
    }
    RPMpulses = 0; 

    RPM = min(RPM_MAX_EVER, RPM);
    RPM = ((RPM / 10) * 10);

    valueFields[F_RPM] = RPM; 
  }  
}

void initRPMCount(void) {
  pinMode(PIO_INTERRUPT_HALL, INPUT_PULLUP); 
  attachInterrupt(digitalPinToInterrupt(PIO_INTERRUPT_HALL), countRPM, CHANGE);  

  rpmTimer = timer_create_default();

  setAccelMaxRPM();
}

void setAccelRPMPercentage(int percentage) {
  currentRPMSolenoid = percentToGivenVal(percentage, PWM_RESOLUTION);
}

void setAccelMaxRPM(void) {
  setAccelRPMPercentage(MAX_RPM_PERCENT_VALUE);
}

bool isEngineThrottlePressed(void) {
  return getEnginePercentageThrottle() > ACCELERATE_MIN_PERCENTAGE_THROTTLE_VALUE;
}

bool cycleCheck(void *argument) {
  rpmCycle = false;
  return false;
}

int getCurrentRPM(void) {
  return (int)valueFields[F_RPM];
}

void stabilizeRPM(void) {
  rpmTimer.tick();

  if(rpmAliveTime < millis()) {
    rpmAliveTime = millis() + RESET_RPM_WATCHDOG_TIME;
    valueFields[F_RPM] = 0;
  }

  int desiredRPM = NOMINAL_RPM_VALUE;

  if(((int)valueFields[F_COOLANT_TEMP]) <= TEMP_COLD_ENGINE) {
    desiredRPM = COLD_RPM_VALUE;
  }

  if(isDPFRegenerating()) {
    desiredRPM = REGEN_RPM_VALUE;
  }

  if(isEngineThrottlePressed() ||
    getCurrentRPM() < RPM_MIN) {  
      desiredRPM = PRESSED_PEDAL_RPM_VALUE;
      setAccelRPMPercentage(ACCELLERATE_RPM_PERCENT_VALUE); //percent
      rpmTimer.cancel();
      rpmCycle = true;
      rpmTimer.in(RPM_TIME_TO_POSITIVE_CORRECTION_RPM_PERCENTAGE, cycleCheck);
      valToPWM(PIO_VP37_RPM, currentRPMSolenoid);
      return;
  }

  if(getCurrentRPM() != desiredRPM) {
    rpmPercentValue = (int)((currentRPMSolenoid * 100) / PWM_RESOLUTION);

    if(getCurrentRPM() < desiredRPM) {
      if(desiredRPM - getCurrentRPM() > MAX_RPM_DIFFERENCE) {

        if(!rpmCycle) {
          rpmCycle = true;

          rpmPercentValue += RPM_PERCENTAGE_CORRECTION_VAL;
          if(rpmPercentValue > MAX_RPM_PERCENT_VALUE){
            rpmPercentValue = MAX_RPM_PERCENT_VALUE;
          }
          setAccelRPMPercentage(rpmPercentValue);
          rpmTimer.in(RPM_TIME_TO_POSITIVE_CORRECTION_RPM_PERCENTAGE, cycleCheck);
        }
      }
    }

    if(getCurrentRPM() > desiredRPM) {
      if(getCurrentRPM() - desiredRPM > MAX_RPM_DIFFERENCE) {

        if(!rpmCycle) {
          rpmCycle = true;

          rpmPercentValue -= RPM_PERCENTAGE_CORRECTION_VAL;
          if(rpmPercentValue < MIN_RPM_PERCENT_VALUE){
            rpmPercentValue = MIN_RPM_PERCENT_VALUE;
          }
          setAccelRPMPercentage(rpmPercentValue);
          rpmTimer.in(RPM_TIME_TO_NEGATIVE_CORRECTION_RPM_PERCENTAGE, cycleCheck);
        }
      }
    }
  }

  valToPWM(PIO_VP37_RPM, currentRPMSolenoid);

#if DEBUG
  deb("rpm:%d current:%d engineLoad:%d", (int)valueFields[F_RPM], currentRPMSolenoid, engineLoad);
#endif
}

bool isEngineRunning(void) {
  return (int(valueFields[F_RPM]) != 0);
}

