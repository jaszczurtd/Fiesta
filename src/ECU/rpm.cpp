#include "rpm.h"

// RPM formula (integer-only, no floats):
// original: pulses * (60000 / RPM_REFRESH_INTERVAL) / CRANK_REVOLUTIONS - RPM_CORRECTION_VAL
//         = pulses * (60000 / 150) / 32 - 50
//         = pulses * 12.5 - 50
//         = (pulses * 25 - 100) / 2
#define RPM_PULSES_MULTIPLIER  25
#define RPM_PULSES_OFFSET      100
#define RPM_PULSES_DIVISOR     2

#ifndef VP37
static SmartTimers rpmCycleTimer;

static void cycleCheckTimerCallback(void) {
  getRPMInstance()->resetRPMCycle();
  // SmartTimers is periodic by default, so stop it after first fire.
  rpmCycleTimer.abort();
}
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

void RPM::resetRPMCycle(void) {
  rpmCycle = false;
}

RPM::RPM() : rpmValue(0) { }

/*
 Hall sensor ISR — called on every crankshaft pulse edge.
 The 150ms time-window check and pulse snapshot MUST stay inside the ISR.
 Moving them to main loop causes jitter because the main loop 
 iteration time varies with CAN, OBD, turbo, etc. workload. That makes
 the counting window non-deterministic and RPM readings unstable.
 hal_millis() is just a timer register read — cheap enough for an ISR.
 Only the integer RPM math is deferred to process() via the rpmReady flag.
*/
void RPM::interrupt(void) {
  unsigned long now = hal_micros();
  unsigned long pulse = now - lastPulse;
  lastPulse = now;

  if ((pulse >> 1) > shortPulse) {
    RPMpulses++;
  }
  shortPulse = pulse;

  unsigned long ms = hal_millis();
  rpmAliveTime = ms + RESET_RPM_WATCHDOG_TIME;
  if (ms - previousMillis >= RPM_REFRESH_INTERVAL) {
    previousMillis = ms;
    snapshotPulses = RPMpulses;
    RPMpulses = 0;
    rpmReady = true;
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
  snapshotPulses = 0;
  rpmReady = false;

  currentRPMSolenoid = 0;
  rpmCycle = false;
#ifndef VP37
  rpmPercentValue = 0;
#endif

  hal_gpio_attach_interrupt(PIO_INTERRUPT_HALL, countRPM, HAL_GPIO_IRQ_CHANGE);

#ifndef VP37
  rpmCycleTimer.begin(cycleCheckTimerCallback, STOP);
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

void RPM::process(void) {
  if (rpmReady) {
    rpmReady = false;

    int rpm = (snapshotPulses * RPM_PULSES_MULTIPLIER - RPM_PULSES_OFFSET) / RPM_PULSES_DIVISOR;
    if (rpm < 0) rpm = 0;
    if (rpm > RPM_MAX_EVER) rpm = RPM_MAX_EVER;
    rpm = (rpm / 10) * 10;

    rpmValue = rpm;
  }

  if (rpmAliveTime < (long)hal_millis()) {
    rpmAliveTime = hal_millis() + RESET_RPM_WATCHDOG_TIME;
    rpmValue = 0;
  }

#ifndef VP37
  rpmCycleTimer.tick();

  int desiredRPM = NOMINAL_RPM_VALUE;

  if(((int)getGlobalValue(F_COOLANT_TEMP)) <= TEMP_COLD_ENGINE) {
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
          rpmCycleTimer.time(RPM_TIME_TO_POSITIVE_CORRECTION_RPM_PERCENTAGE);
          rpmCycleTimer.restart();
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
          rpmCycleTimer.time(RPM_TIME_TO_NEGATIVE_CORRECTION_RPM_PERCENTAGE);
          rpmCycleTimer.restart();
        }
      }
    }
  }

  valToPWM(PIO_VP37_RPM, currentRPMSolenoid);

#if DEBUG
  showDebug();
#endif

#endif /*VP37*/
}

bool RPM::isEngineRunning(void) {
  return (getCurrentRPM() != 0);
}

void RPM::showDebug() {
  deb("rpm:%d current:%d", getCurrentRPM(), currentRPMSolenoid);
}
