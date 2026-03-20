#include "rpm.h"

#define CRANK_REVOLUTIONS 32.0

#ifndef VP37
static Timer rpmTimer;
#endif

static RPM engineRPM;
void createRPM(void) {
  engineRPM.init();
}

RPM *getRPMInstance(void) {
  return &engineRPM;
}

void countRPM(void) {
  getRPMInstance()->interrupt();
}

bool cycleCheck(void *argument) {
  getRPMInstance()->resetRPMCycle();
  return false;
}

void RPM::resetRPMCycle(void) {
  rpmCycle = false;
}

RPM::RPM() : rpmValue(0) { }

void RPM::interrupt(void) {
  unsigned long _micros = hal_micros();
  unsigned long nowPulse = _micros - lastPulse;

  lastPulse = _micros;

  if((nowPulse >> 1) > shortPulse){
    RPMpulses++;
    shortPulse = nowPulse;
  } else {
    shortPulse = nowPulse;
  }

  unsigned long _millis = hal_millis();
  rpmAliveTime = _millis + RESET_RPM_WATCHDOG_TIME;
  if(_millis - previousMillis >= RPM_REFRESH_INTERVAL) {
    previousMillis = _millis;

    int rpm = int((RPMpulses * (MILIS_IN_MINUTE / float(RPM_REFRESH_INTERVAL))) / CRANK_REVOLUTIONS) - RPM_CORRECTION_VAL;
    if(rpm < 0) {
      rpm = 0;
    }
    RPMpulses = 0;

    rpm = min(RPM_MAX_EVER, rpm);
    rpm = ((rpm / 10) * 10);

    rpmValue = rpm;
  }
}

void RPM::init(void) {
  hal_gpio_set_mode(PIO_INTERRUPT_HALL, HAL_GPIO_INPUT_PULLUP);

  rpmValue = 0;
  previousMillis = 0;
  shortPulse = 0;
  lastPulse = 0;
  rpmAliveTime = 0;
  RPMpulses = 0;

  currentRPMSolenoid = 0;
  rpmCycle = false;
#ifndef VP37
  rpmPercentValue = 0;
#endif

  hal_gpio_attach_interrupt(PIO_INTERRUPT_HALL, countRPM, HAL_GPIO_IRQ_CHANGE);

#ifndef VP37
  rpmTimer = timer_create_default();
#endif

  setAccelMaxRPM();
}

void RPM::setAccelRPMPercentage(int percentage) {
  currentRPMSolenoid = percentToGivenVal(percentage, PWM_RESOLUTION);
}

int RPM::getCurrentRPMSolenoid(void) {
  return currentRPMSolenoid;
}

void RPM::setAccelMaxRPM(void) {
  setAccelRPMPercentage(MAX_RPM_PERCENT_VALUE);
}

bool RPM::isEngineThrottlePressed(void) {
  return getThrottlePercentage() > ACCELERATE_MIN_PERCENTAGE_THROTTLE_VALUE;
}

int RPM::getCurrentRPM(void) {
  return rpmValue;
}

#ifndef VP37
void RPM::process(void) {
  rpmTimer.tick();

  if(rpmAliveTime < (long)hal_millis()) {
    rpmAliveTime = hal_millis() + RESET_RPM_WATCHDOG_TIME;
    rpmValue = 0;
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
  showDebug();
#endif
}
#else
  void RPM::process(void) { }
#endif

bool RPM::isEngineRunning(void) {
  return (getCurrentRPM() != 0);
}

void RPM::showDebug() {
  deb("rpm:%d current:%d", getCurrentRPM(), currentRPMSolenoid);
}
