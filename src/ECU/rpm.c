#include "rpm.h"
#include "ecuContext.h"
#include <hal/hal_soft_timer.h>

// RPM formula (integer-only, no floats):
// original: pulses * (60000 / RPM_REFRESH_INTERVAL) / CRANK_REVOLUTIONS - RPM_CORRECTION_VAL
//         = pulses * (60000 / 150) / 32 - 50
//         = pulses * 12.5 - 50
//         = (pulses * 25 - 100) / 2
#define RPM_PULSES_MULTIPLIER  25
#define RPM_PULSES_OFFSET      100
#define RPM_PULSES_DIVISOR     2

#ifndef VP37
static void cycleCheckTimerCallback(void) {
  RPM_resetRPMCycle(getRPMInstance());
  // Software timer is periodic by default, so stop it after first fire.
  hal_soft_timer_abort(getRPMInstance()->rpmCycleTimer);
}
#endif

void RPM_create(void) {
  RPM_init(getRPMInstance());
}

RPM *getRPMInstance(void) {
  return &getECUContext()->rpm;
}

void countRPM(void) {
  RPM_interrupt(getRPMInstance());
}

void RPM_resetRPMCycle(RPM *self) {
  self->rpmCycle = false;
}

/*
 Hall sensor ISR — called on every crankshaft pulse edge.
 The 150ms time-window check and pulse snapshot MUST stay inside the ISR.
 Moving them to main loop causes jitter because the main loop
 iteration time varies with CAN, OBD, turbo, etc. workload. That makes
 the counting window non-deterministic and RPM readings unstable.
 hal_millis() is just a timer register read — cheap enough for an ISR.
 Only the integer RPM math is deferred to process() via the rpmReady flag.
*/
void RPM_interrupt(RPM *self) {
  unsigned long now = hal_micros();
  unsigned long pulse = now - self->lastPulse;
  self->lastPulse = now;

  if ((pulse >> 1) > self->shortPulse) {
    self->RPMpulses++;
  }
  self->shortPulse = pulse;

  unsigned long ms = hal_millis();
  self->rpmAliveTime = ms + RESET_RPM_WATCHDOG_TIME;
  if (ms - self->previousMillis >= RPM_REFRESH_INTERVAL) {
    self->previousMillis = ms;
    self->snapshotPulses = self->RPMpulses;
    self->RPMpulses = 0;
    self->rpmReady = true;
  }
}

void RPM_init(RPM *self) {
  hal_gpio_set_mode(PIO_INTERRUPT_HALL, HAL_GPIO_INPUT_PULLUP);

  self->rpmValue = 0;
  self->previousMillis = 0;
  self->shortPulse = 0;
  self->lastPulse = 0;
  self->rpmAliveTime = 0;
  self->RPMpulses = 0;
  self->snapshotPulses = 0;
  self->rpmReady = false;

  self->currentRPMSolenoid = 0;
  self->rpmCycle = false;
#ifndef VP37
  self->rpmPercentValue = 0;
  self->rpmCycleTimer = NULL;
#endif

  hal_gpio_attach_interrupt(PIO_INTERRUPT_HALL, countRPM, HAL_GPIO_IRQ_CHANGE);

#ifndef VP37
  if(self->rpmCycleTimer == NULL) {
    self->rpmCycleTimer = hal_soft_timer_create();
  }
  (void)hal_soft_timer_begin(self->rpmCycleTimer, cycleCheckTimerCallback, HAL_SOFT_TIMER_STOP);
#endif

  RPM_setAccelMaxRPM(self);
}

void RPM_setAccelRPMPercentage(RPM *self, int32_t percentage) {
  self->currentRPMSolenoid = percentToGivenVal(percentage, PWM_RESOLUTION);
}

int32_t RPM_getCurrentRPMSolenoid(const RPM *self) {
  return self->currentRPMSolenoid;
}

void RPM_setAccelMaxRPM(RPM *self) {
  RPM_setAccelRPMPercentage(self, MAX_RPM_PERCENT_VALUE);
}

#ifndef VP37
static bool RPM_isEngineThrottlePressed(RPM *self) {
  (void)self;
  return getThrottlePercentage() > ACCELERATE_MIN_PERCENTAGE_THROTTLE_VALUE;
}
#endif

int32_t RPM_getCurrentRPM(const RPM *self) {
  return self->rpmValue;
}

void RPM_process(RPM *self) {
  if (self->rpmReady) {
    self->rpmReady = false;

    int32_t rpm = (self->snapshotPulses * RPM_PULSES_MULTIPLIER - RPM_PULSES_OFFSET) / RPM_PULSES_DIVISOR;
    if (rpm < 0) rpm = 0;
    if (rpm > RPM_MAX_EVER) rpm = RPM_MAX_EVER;
    rpm = (rpm / 10) * 10;

    self->rpmValue = rpm;
  }

  if (self->rpmAliveTime < (long)hal_millis()) {
    self->rpmAliveTime = hal_millis() + RESET_RPM_WATCHDOG_TIME;
    self->rpmValue = 0;
  }

#ifndef VP37
  hal_soft_timer_tick(self->rpmCycleTimer);

  int desiredRPM = NOMINAL_RPM_VALUE;

  if(((int32_t)getGlobalValue(F_COOLANT_TEMP)) <= TEMP_COLD_ENGINE) {
    desiredRPM = COLD_RPM_VALUE;
  }

  if(isDPFRegenerating()) {
    desiredRPM = REGEN_RPM_VALUE;
  }

  if(RPM_isEngineThrottlePressed(self) ||
    RPM_getCurrentRPM(self) < RPM_MIN) {
      RPM_setAccelRPMPercentage(self, ACCELLERATE_RPM_PERCENT_VALUE); //percent
      valToPWM(PIO_VP37_RPM, self->currentRPMSolenoid);
      return;
  }

  if(RPM_getCurrentRPM(self) != desiredRPM) {
    self->rpmPercentValue = (int32_t)((self->currentRPMSolenoid * 100) / PWM_RESOLUTION);

    if(RPM_getCurrentRPM(self) < desiredRPM) {
      if(desiredRPM - RPM_getCurrentRPM(self) > MAX_RPM_DIFFERENCE) {

        if(!self->rpmCycle) {
          self->rpmCycle = true;

          self->rpmPercentValue += RPM_PERCENTAGE_CORRECTION_VAL;
          if(self->rpmPercentValue > MAX_RPM_PERCENT_VALUE){
            self->rpmPercentValue = MAX_RPM_PERCENT_VALUE;
          }
          RPM_setAccelRPMPercentage(self, self->rpmPercentValue);
          hal_soft_timer_set_interval(self->rpmCycleTimer, RPM_TIME_TO_POSITIVE_CORRECTION_RPM_PERCENTAGE);
          hal_soft_timer_restart(self->rpmCycleTimer);
        }
      }
    }

    if(RPM_getCurrentRPM(self) > desiredRPM) {
      if(RPM_getCurrentRPM(self) - desiredRPM > MAX_RPM_DIFFERENCE) {

        if(!self->rpmCycle) {
          self->rpmCycle = true;

          self->rpmPercentValue -= RPM_PERCENTAGE_CORRECTION_VAL;
          if(self->rpmPercentValue < MIN_RPM_PERCENT_VALUE){
            self->rpmPercentValue = MIN_RPM_PERCENT_VALUE;
          }
          RPM_setAccelRPMPercentage(self, self->rpmPercentValue);
          hal_soft_timer_set_interval(self->rpmCycleTimer, RPM_TIME_TO_NEGATIVE_CORRECTION_RPM_PERCENTAGE);
          hal_soft_timer_restart(self->rpmCycleTimer);
        }
      }
    }
  }

  valToPWM(PIO_VP37_RPM, self->currentRPMSolenoid);

#if DEBUG
  RPM_showDebug(self);
#endif

#endif /*VP37*/
}

bool RPM_isEngineRunning(const RPM *self) {
  return (RPM_getCurrentRPM(self) != 0);
}

void RPM_showDebug(RPM *self) {
  deb("rpm:%d current:%d", RPM_getCurrentRPM(self), self->currentRPMSolenoid);
}
