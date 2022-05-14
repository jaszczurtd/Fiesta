#include "rpm.h"

Timer rpmTimer;

static unsigned long previousMillis = 0;
static volatile int RPMpulses = 0;
static volatile unsigned long shortPulse = 0;
static volatile unsigned long lastPulse = 0;
static long rpmAliveTime = 0;

void countRPM(void) {
  unsigned long now = micros();
  unsigned long nowPulse = now - lastPulse;
  
  lastPulse = now;

  if((nowPulse >> 1) > shortPulse){ 
    RPMpulses++;
    shortPulse = nowPulse; 
  } else { 
    shortPulse = nowPulse;
  }

  rpmAliveTime = millis() + RESET_RPM_WATCHDOG_TIME;

  if(millis() - previousMillis > RPM_REFRESH_INTERVAL) {
    previousMillis = millis();

    int RPM = int(RPMpulses * (60000.0 / float(RPM_REFRESH_INTERVAL)) * 4 / 4 / 32.0) - 100; 
    if(RPM < 0) {
      RPM = 0;
    }
    RPMpulses = 0; 

    RPM = min(99999, RPM);
    RPM = ((RPM / 10) * 10);

    valueFields[F_RPM] = RPM; 
  }  

}

static int currentRPMSolenoid = 0;
static bool rpmCycle = false;
static int rpmPercentValue = 0;

void initRPMCount(void) {
  pinMode(INTERRUPT_HALL, INPUT_PULLUP); 
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_HALL), countRPM, CHANGE);  

  rpmTimer = timer_create_default();

  setMaxRPM();
}

void setMaxRPM(void) {
  currentRPMSolenoid = percentToGivenVal(100, PWM_RESOLUTION);
  valToPWM(9, currentRPMSolenoid);
}

bool cycleCheck(void *argument) {
  rpmCycle = false;
  return false;
}

void stabilizeRPM(void) {
  rpmTimer.tick();

  if(rpmAliveTime < millis()) {
    rpmAliveTime = millis() + RESET_RPM_WATCHDOG_TIME;
    valueFields[F_RPM] = 0;
  }

  int engineLoad = getEnginePercentageLoad();
  if(engineLoad > 5) {  //percent
    setMaxRPM();
    rpmPercentValue = 0;
    rpmCycle = false;
    return;
  }

  int rpm = (int)valueFields[F_RPM];
  if(rpm != NOMINAL_RPM_VALUE) {
    rpmPercentValue = (int)((currentRPMSolenoid * 100) / PWM_RESOLUTION);

    if(rpm < NOMINAL_RPM_VALUE) {
      if(NOMINAL_RPM_VALUE - rpm > MAX_RPM_DIFFERENCE) {

        if(!rpmCycle) {
          rpmCycle = true;

          rpmPercentValue += 10;
          if(rpmPercentValue > 100){
            rpmPercentValue = 100;
          }
          currentRPMSolenoid = percentToGivenVal(rpmPercentValue, PWM_RESOLUTION);
          rpmTimer.in(250, cycleCheck);
        }
      }
    }

    if(rpm > NOMINAL_RPM_VALUE) {
      if(rpm - NOMINAL_RPM_VALUE > MAX_RPM_DIFFERENCE) {

        if(!rpmCycle) {
          rpmCycle = true;

          rpmPercentValue -= 10;
          if(rpmPercentValue < 0){
            rpmPercentValue = 0;
          }
          currentRPMSolenoid = percentToGivenVal(rpmPercentValue, PWM_RESOLUTION);
          rpmTimer.in(500, cycleCheck);
        }
      }
    }

    valToPWM(9, currentRPMSolenoid);
  }


//#if DEBUG
  char buf[128];
  snprintf(buf, sizeof(buf) - 1, "rpm:%d current:%d engineLoad:%d", (int)valueFields[F_RPM], currentRPMSolenoid, engineLoad);
  //Serial.println(buf);
//#endif
}

