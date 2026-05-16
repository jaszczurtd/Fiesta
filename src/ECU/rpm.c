#include "rpm.h"
#include "ecuContext.h"
#include <hal/hal_soft_timer.h>

// RPM formula (integer-only, no floats):
// rpm = pulses * 60000 / (RPM_REFRESH_INTERVAL * RPM_PULSES_PER_REVOLUTION)
//       - RPM_CORRECTION_VAL
// Keep this derived from RPM_REFRESH_INTERVAL so timing tuning cannot desync
// RPM conversion scaling.
#define RPM_PULSES_PER_REVOLUTION  32L
#define RPM_NUMERATOR_PER_MINUTE   60000L
#define RPM_DENOMINATOR            ((int64_t)RPM_REFRESH_INTERVAL * RPM_PULSES_PER_REVOLUTION)

#ifndef VP37
static unsigned long s_lastPiStepAtMs = 0;
static int32_t s_lastPTermQ10 = 0;
static int32_t s_lastIDeltaQ10 = 0;

/**
 * @brief Reset the temporary RPM correction cycle after the timer fires.
 * @return None.
 */
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

/**
 * @brief Forward a Hall-sensor edge interrupt to the shared G28-like RPM instance.
 * @return None.
 */
void countRPM(void) {
  RPM_interrupt(getRPMInstance());
}

void RPM_resetRPMCycle(RPM *self) {
  self->rpmCycle = false;
}

/*
 G28-like crank sensor ISR - called on every crankshaft pulse edge.
 The 150ms time-window check and pulse snapshot MUST stay inside the ISR.
 Moving them to main loop causes jitter because the main loop
 iteration time varies with CAN, OBD, turbo, etc. workload. That makes
 the counting window non-deterministic and RPM readings unstable.
 hal_millis() is just a timer register read - cheap enough for an ISR.
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
  self->rpmFiltered = 0;
  self->rpmIntegratorQ10 = 0;
  self->piInitialized = false;
  self->wasEngineRunning = false;
  self->vacuumReady = false;
  self->vacuumReadyAt = 0;
  self->rpmCycleTimer = NULL;
  s_lastPiStepAtMs = 0;
  s_lastPTermQ10 = 0;
  s_lastIDeltaQ10 = 0;
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
/**
 * @brief Check whether legacy throttle / driver-demand input exceeds the non-VP37 acceleration threshold.
 * @param self RPM controller instance using the throttle signal.
 * @return True when throttle is above the configured threshold, otherwise false.
 */
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

    int64_t scaled = ((int64_t)self->snapshotPulses * RPM_NUMERATOR_PER_MINUTE
      + (RPM_DENOMINATOR / 2)) / RPM_DENOMINATOR;
    int32_t rpm = (int32_t)scaled - RPM_CORRECTION_VAL;
    if (rpm < 0) rpm = 0;
    if (rpm > RPM_MAX_EVER) rpm = RPM_MAX_EVER;

    self->rpmValue = rpm;
#ifndef VP37
    if (rpm == 0) {
      self->rpmFiltered = 0;
    } else if (self->rpmFiltered == 0) {
      self->rpmFiltered = rpm;
    } else {
      self->rpmFiltered = (self->rpmFiltered * (RPM_CONTROL_FILTER_WINDOW - 1) + rpm)
        / RPM_CONTROL_FILTER_WINDOW;
    }
#endif
  }

  if (self->rpmAliveTime < (long)hal_millis()) {
    self->rpmAliveTime = hal_millis() + RESET_RPM_WATCHDOG_TIME;
    self->rpmValue = 0;
#ifndef VP37
    self->rpmFiltered = 0;
#endif
  }

#ifndef VP37
  hal_soft_timer_tick(self->rpmCycleTimer);

  unsigned long now = hal_millis();
  int32_t currentRPM = RPM_getCurrentRPM(self);
  bool engineRunning = (currentRPM >= RPM_MIN);

  if (engineRunning && !self->wasEngineRunning) {
    self->vacuumReady = false;
    self->vacuumReadyAt = now + RPM_CONTROL_VACUUM_FILL_MS;
    self->rpmCycle = false;
    self->piInitialized = false;
    self->rpmIntegratorQ10 = 0;
  } else if (!engineRunning) {
    self->vacuumReady = false;
    self->vacuumReadyAt = 0;
    self->rpmCycle = false;
    self->piInitialized = false;
    self->rpmIntegratorQ10 = 0;
  }
  self->wasEngineRunning = engineRunning;

  if (!self->vacuumReady && engineRunning) {
    if ((long)(now - self->vacuumReadyAt) >= 0) {
      self->vacuumReady = true;
    }
  }

  int desiredRPM = ecuParamsNominalRpm();

  if(((int32_t)getGlobalValue(F_COOLANT_TEMP)) <= TEMP_COLD_ENGINE) {
    desiredRPM = COLD_RPM_VALUE;
  }

  if(isDPFRegenerating()) {
    desiredRPM = REGEN_RPM_VALUE;
  }

  if(RPM_isEngineThrottlePressed(self) ||
    currentRPM < RPM_MIN) {
      RPM_setAccelRPMPercentage(self, ACCELLERATE_RPM_PERCENT_VALUE); //percent
      self->rpmPercentValue = ACCELLERATE_RPM_PERCENT_VALUE;
      self->piInitialized = false;
      self->rpmIntegratorQ10 = 0;
      self->rpmCycle = false;
      s_lastPTermQ10 = 0;
      s_lastIDeltaQ10 = 0;
      valToPWM(PIO_VP37_RPM, self->currentRPMSolenoid);
      return;
  }

  if (!self->vacuumReady) {
    self->rpmPercentValue = (int32_t)((self->currentRPMSolenoid * 100) / PWM_RESOLUTION);
    self->piInitialized = false;
    self->rpmIntegratorQ10 = 0;
    self->rpmCycle = false;
    s_lastPTermQ10 = 0;
    s_lastIDeltaQ10 = 0;
    valToPWM(PIO_VP37_RPM, self->currentRPMSolenoid);
    return;
  }

  if (self->rpmCycle) {
    valToPWM(PIO_VP37_RPM, self->currentRPMSolenoid);
    return;
  }

  int32_t rpmForControl = (self->rpmFiltered > 0) ? self->rpmFiltered : currentRPM;
  int32_t error = desiredRPM - rpmForControl;

  if (error <= RPM_PI_DEADBAND_RPM && error >= -RPM_PI_DEADBAND_RPM) {
    s_lastPTermQ10 = 0;
    s_lastIDeltaQ10 = 0;
    valToPWM(PIO_VP37_RPM, self->currentRPMSolenoid);
    return;
  }

  int32_t minQ10 = MIN_RPM_PERCENT_VALUE * RPM_PI_Q10_SCALE;
  int32_t maxQ10 = MAX_RPM_PERCENT_VALUE * RPM_PI_Q10_SCALE;

  if (!self->piInitialized) {
    self->rpmPercentValue = (int32_t)((self->currentRPMSolenoid * 100) / PWM_RESOLUTION);
    if (self->rpmPercentValue > MAX_RPM_PERCENT_VALUE) {
      self->rpmPercentValue = MAX_RPM_PERCENT_VALUE;
    } else if (self->rpmPercentValue < MIN_RPM_PERCENT_VALUE) {
      self->rpmPercentValue = MIN_RPM_PERCENT_VALUE;
    }
    self->rpmIntegratorQ10 = self->rpmPercentValue * RPM_PI_Q10_SCALE;
    self->piInitialized = true;
  }

  int32_t pTermQ10 = error * RPM_PI_KP_Q10;
  int32_t iDeltaQ10 = (int32_t)(((int64_t)error * RPM_PI_KI_Q10 * RPM_PI_UPDATE_INTERVAL_MS) / 1000);
  int32_t uUnsatQ10 = self->rpmIntegratorQ10 + pTermQ10;
  bool atHigh = (uUnsatQ10 > maxQ10);
  bool atLow = (uUnsatQ10 < minQ10);

  if ((atHigh && error > 0) || (atLow && error < 0)) {
    iDeltaQ10 = 0;
  }

  int64_t nextIntegratorQ10 = (int64_t)self->rpmIntegratorQ10 + (int64_t)iDeltaQ10;
  if (nextIntegratorQ10 > maxQ10) {
    nextIntegratorQ10 = maxQ10;
  } else if (nextIntegratorQ10 < minQ10) {
    nextIntegratorQ10 = minQ10;
  }
  self->rpmIntegratorQ10 = (int32_t)nextIntegratorQ10;

  int64_t uQ10 = (int64_t)self->rpmIntegratorQ10 + pTermQ10;
  if (uQ10 > maxQ10) {
    uQ10 = maxQ10;
  } else if (uQ10 < minQ10) {
    uQ10 = minQ10;
  }

  s_lastPTermQ10 = pTermQ10;
  s_lastIDeltaQ10 = iDeltaQ10;

  self->rpmPercentValue = (int32_t)(uQ10 / RPM_PI_Q10_SCALE);
  RPM_setAccelRPMPercentage(self, self->rpmPercentValue);
  s_lastPiStepAtMs = now;
  self->rpmCycle = true;
  hal_soft_timer_set_interval(self->rpmCycleTimer, RPM_PI_UPDATE_INTERVAL_MS);
  hal_soft_timer_restart(self->rpmCycleTimer);

  valToPWM(PIO_VP37_RPM, self->currentRPMSolenoid);

#endif /*VP37*/
}

bool RPM_isEngineRunning(const RPM *self) {
  return (RPM_getCurrentRPM(self) != 0);
}

void RPM_showDebug(RPM *self) {
#ifndef VP37
  static unsigned long lastPeriodicLogMs = 0;
  unsigned long now = hal_millis();
  if (now - lastPeriodicLogMs < RPM_DEBUG_UPDATE_MS) {
    return;
  }
  lastPeriodicLogMs = now;

  int desiredRPM = ecuParamsNominalRpm();
  bool coldEngine = ((int32_t)getGlobalValue(F_COOLANT_TEMP)) <= TEMP_COLD_ENGINE;
  if (coldEngine) {
    desiredRPM = COLD_RPM_VALUE;
  }
  bool regenActive = isDPFRegenerating();
  if (regenActive) {
    desiredRPM = REGEN_RPM_VALUE;
  }

  int32_t rpmForControl = (self->rpmFiltered > 0) ? self->rpmFiltered : RPM_getCurrentRPM(self);
  int32_t error = desiredRPM - rpmForControl;
  int32_t iPercent = self->rpmIntegratorQ10 / RPM_PI_Q10_SCALE;
  int32_t outPercent = (int32_t)((self->currentRPMSolenoid * 100) / PWM_RESOLUTION);
  int32_t pTermDeciPct = (s_lastPTermQ10 * 10) / RPM_PI_Q10_SCALE;
  int32_t iDeltaDeciPct = (s_lastIDeltaQ10 * 10) / RPM_PI_Q10_SCALE;
  unsigned long piStepAgoMs = (s_lastPiStepAtMs == 0)
    ? 0
    : (now - s_lastPiStepAtMs);

  deb("rpmPI rpm:%d filt:%d des:%d err:%d out:%d cmd:%d i:%d vac:%d cyc:%d stepAgo:%lu p10:%d iD10:%d thr:%d regen:%d cold:%d",
      RPM_getCurrentRPM(self),
      self->rpmFiltered,
      desiredRPM,
      error,
      self->rpmPercentValue,
      outPercent,
      iPercent,
      self->vacuumReady ? 1 : 0,
      self->rpmCycle ? 1 : 0,
      piStepAgoMs,
      pTermDeciPct,
      iDeltaDeciPct,
      RPM_isEngineThrottlePressed(self) ? 1 : 0,
      regenActive ? 1 : 0,
      coldEngine ? 1 : 0);
#else
  deb("rpm:%d current:%d", RPM_getCurrentRPM(self), self->currentRPMSolenoid);
#endif
}
