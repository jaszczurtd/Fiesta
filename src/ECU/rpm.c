#include "rpm.h"
#include "ecuContext.h"
#include <hal/gpio/hal_gpio.h>
#include <hal/system/hal_sync.h>
#include <hal/timers/hal_soft_timer.h>

#define RPM_MICROS_PER_MINUTE UINT64_C(60000000)
#define RPM_CAPTURE_MAX_RPM 20000u
#define RPM_MIN_PERIOD_US                                                      \
  ((60000000u + (RPM_PULSES_PER_REVOLUTION * RPM_CAPTURE_MAX_RPM) - 1u) /      \
   (RPM_PULSES_PER_REVOLUTION * RPM_CAPTURE_MAX_RPM))
#define RPM_AVERAGING_WINDOW_US 25000u
#define RPM_TIMEOUT_MIN_US 20000u
#define RPM_TIMEOUT_MAX_US 500000u

JB_STATIC_ASSERT(RPM_PULSES_PER_REVOLUTION > 0u, "RPM needs a pulse count");
JB_STATIC_ASSERT((RPM_CAPTURE_SAMPLES > 1u) &&
                     (RPM_CAPTURE_SAMPLES <= UINT8_MAX),
                 "RPM capture index must fit uint8_t");

static uint32_t RPM_signalTimeout(uint32_t periodUs) {
  // periodUs is below the previous timeout, hence below 500000 us.
  uint32_t timeout = periodUs * 3u;
  if (timeout < RPM_TIMEOUT_MIN_US) {
    timeout = RPM_TIMEOUT_MIN_US;
  }
  if (timeout > RPM_TIMEOUT_MAX_US) {
    timeout = RPM_TIMEOUT_MAX_US;
  }
  return timeout;
}

#ifndef VP37
static unsigned long s_lastPiStepAtMs = 0;
static int32_t s_lastPTermQ10 = 0;
static int32_t s_lastIDeltaQ10 = 0;

/**
 * @brief Reset the temporary RPM correction cycle after the timer fires.
 */
static void cycleCheckTimerCallback(void) {
  RPM_resetRPMCycle(getRPMInstance());
  // Software timer is periodic by default, so stop it after first fire.
  hal_soft_timer_abort(getRPMInstance()->rpmCycleTimer);
}
#endif

hal_status_t RPM_create(void) { return RPM_init(getRPMInstance()); }

RPM *getRPMInstance(void) { return &getECUContext()->rpm; }

/**
 * @brief Forward a Hall-sensor edge interrupt to the shared G28-like RPM
 * instance.
 */
static void countRPM(void) { RPM_interrupt(getRPMInstance()); }

void RPM_resetRPMCycle(RPM *self) { self->rpmCycle = false; }

void RPM_interrupt(RPM *self) {
  const uint32_t now = hal_micros();
  bool accepted = true;
  if (self->capture.count != 0u) {
    const uint32_t last = self->capture.timestamps[self->capture.latest];
    if (hal_elapsed_u32(now, last, self->capture.timeoutUs)) {
      self->capture.count = 0u;
    } else if (hal_elapsed_u32(now, last, RPM_MIN_PERIOD_US)) {
      self->capture.timeoutUs = RPM_signalTimeout(now - last);
    } else {
      accepted = false;
    }
  }
  if (accepted) {
    if (self->capture.count == 0u) {
      self->capture.latest = 0u;
      self->capture.timeoutUs = RPM_TIMEOUT_MAX_US;
    } else {
      const uint8_t next = (uint8_t)(self->capture.latest + 1u);
      self->capture.latest =
          (next < COUNTOF(self->capture.timestamps)) ? next : 0u;
    }
    self->capture.timestamps[self->capture.latest] = now;
    if (self->capture.count < COUNTOF(self->capture.timestamps)) {
      ++self->capture.count;
    }
    ++self->capture.sequence;
  }
}

hal_status_t RPM_init(RPM *self) {
  if (self == NULL) {
    return HAL_EINVAL;
  }

  hal_gpio_set_mode(JH_HW_NODE_HALL_PIN_IN, HAL_GPIO_INPUT_PULLUP);

  HAL_ATOMIC_STORE(&self->rpmValue, 0, HAL_ATOMIC_RELAXED);
  static const RPMCapture empty = {.timeoutUs = RPM_TIMEOUT_MAX_US};
  self->capture = empty;
  self->processedSequence = 0u;

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

#ifndef VP37
  if (self->rpmCycleTimer == NULL) {
    self->rpmCycleTimer = hal_soft_timer_create();
  }
  if (self->rpmCycleTimer == NULL) {
    return HAL_ENOMEM;
  }
  if (!hal_soft_timer_begin(self->rpmCycleTimer, cycleCheckTimerCallback,
                            HAL_SOFT_TIMER_STOP)) {
    hal_soft_timer_destroy(self->rpmCycleTimer);
    self->rpmCycleTimer = NULL;
    return HAL_EINTERNAL;
  }
#endif

#if HAL_TARGET_IS_RP
  const hal_status_t irqStatus =
      hal_gpio_attach_interrupt_ex(JH_HW_NODE_HALL_PIN_IN, countRPM,
                                   HAL_GPIO_IRQ_RISING, RPM_IRQ_OWNER_CORE);
#else
  const hal_status_t irqStatus = hal_gpio_attach_interrupt_ex(
      JH_HW_NODE_HALL_PIN_IN, countRPM, HAL_GPIO_IRQ_RISING, 0);
#endif

  if (irqStatus != HAL_OK) {
#ifndef VP37
    hal_soft_timer_destroy(self->rpmCycleTimer);
    self->rpmCycleTimer = NULL;
#endif
    return irqStatus;
  }

  RPM_setAccelMaxRPM(self);
  return HAL_OK;
}

void RPM_setAccelRPMPercentage(RPM *self, int32_t percentage) {
  self->currentRPMSolenoid =
      hal_math_percent_to_value((float)percentage, PWM_RESOLUTION);
}

int32_t RPM_getCurrentRPMSolenoid(const RPM *self) {
  return self->currentRPMSolenoid;
}

void RPM_setAccelMaxRPM(RPM *self) {
  RPM_setAccelRPMPercentage(self, MAX_RPM_PERCENT_VALUE);
}

#ifndef VP37
/**
 * @brief Check whether legacy throttle / driver-demand input exceeds the
 * non-VP37 acceleration threshold.
 * @param self RPM controller instance using the throttle signal.
 * @return True when throttle is above the configured threshold, otherwise
 * false.
 */
static bool RPM_isEngineThrottlePressed(RPM *self) {
  (void)self;
  return getThrottlePercentage() > ACCELERATE_MIN_PERCENTAGE_THROTTLE_VALUE;
}
#endif

int32_t RPM_getCurrentRPM(const RPM *self) {
  return HAL_ATOMIC_LOAD(&self->rpmValue, HAL_ATOMIC_RELAXED);
}

void RPM_process(RPM *self) {
  // The ISR is on this core: mask only while copying its small capture.
  hal_critical_section_enter();
  const uint32_t now = hal_micros();
  if ((self->capture.count != 0u) &&
      hal_elapsed_u32(now, self->capture.timestamps[self->capture.latest],
                      self->capture.timeoutUs)) {
    self->capture.count = 0u;
  }
  RPMCapture capture;
  capture = self->capture;
  hal_critical_section_exit();

  if (capture.count < 2u) {
    HAL_ATOMIC_STORE(&self->rpmValue, 0, HAL_ATOMIC_RELAXED);
#ifndef VP37
    self->rpmFiltered = 0;
#endif
  } else if (capture.sequence != self->processedSequence) {
    const uint32_t latest = capture.timestamps[capture.latest];
    uint32_t elapsed = 0u;
    uint32_t periods = 0u;
    uint8_t index = capture.latest;
    for (uint8_t i = 1u; i < capture.count; ++i) {
      index = (index == 0u) ? (uint8_t)(COUNTOF(capture.timestamps) - 1u)
                            : (uint8_t)(index - 1u);
      const uint32_t span = latest - capture.timestamps[index];
      if ((i > 1u) && (span > RPM_AVERAGING_WINDOW_US)) {
        break;
      }
      elapsed = span;
      periods = i;
    }
    const uint64_t denominator = (uint64_t)elapsed * RPM_PULSES_PER_REVOLUTION;
    const uint64_t numerator =
        (RPM_MICROS_PER_MINUTE * periods) + (denominator / 2u);
    int32_t rpm = 0;
    if (denominator != 0u) {
      const uint64_t scaled = numerator / denominator;
      rpm = (int32_t)scaled;
    }
    HAL_ATOMIC_STORE(&self->rpmValue, rpm, HAL_ATOMIC_RELAXED);
#ifndef VP37
    if (rpm == 0) {
      self->rpmFiltered = 0;
    } else if (self->rpmFiltered == 0) {
      self->rpmFiltered = rpm;
    } else {
      self->rpmFiltered =
          (self->rpmFiltered * (RPM_CONTROL_FILTER_WINDOW - 1) + rpm) /
          RPM_CONTROL_FILTER_WINDOW;
    }
#endif
  } else {
    // No new accepted edge: retain the current measurement until timeout.
  }

  self->processedSequence = capture.sequence;

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

  if (((int32_t)getGlobalValue(F_COOLANT_TEMP)) <= TEMP_COLD_ENGINE) {
    desiredRPM = COLD_RPM_VALUE;
  }

  if (isDPFRegenerating()) {
    desiredRPM = REGEN_RPM_VALUE;
  }

  if (RPM_isEngineThrottlePressed(self) || currentRPM < RPM_MIN) {
    RPM_setAccelRPMPercentage(self, ACCELLERATE_RPM_PERCENT_VALUE); // percent
    self->rpmPercentValue = ACCELLERATE_RPM_PERCENT_VALUE;
    self->piInitialized = false;
    self->rpmIntegratorQ10 = 0;
    self->rpmCycle = false;
    s_lastPTermQ10 = 0;
    s_lastIDeltaQ10 = 0;
    valToPWM(JH_HW_NODE_VP37_PIN_QUANTITY_PWM, self->currentRPMSolenoid);
    return;
  }

  if (!self->vacuumReady) {
    self->rpmPercentValue =
        (int32_t)((self->currentRPMSolenoid * 100) / PWM_RESOLUTION);
    self->piInitialized = false;
    self->rpmIntegratorQ10 = 0;
    self->rpmCycle = false;
    s_lastPTermQ10 = 0;
    s_lastIDeltaQ10 = 0;
    valToPWM(JH_HW_NODE_VP37_PIN_QUANTITY_PWM, self->currentRPMSolenoid);
    return;
  }

  if (self->rpmCycle) {
    valToPWM(JH_HW_NODE_VP37_PIN_QUANTITY_PWM, self->currentRPMSolenoid);
    return;
  }

  int32_t rpmForControl =
      (self->rpmFiltered > 0) ? self->rpmFiltered : currentRPM;
  int32_t error = desiredRPM - rpmForControl;

  if (error <= RPM_PI_DEADBAND_RPM && error >= -RPM_PI_DEADBAND_RPM) {
    s_lastPTermQ10 = 0;
    s_lastIDeltaQ10 = 0;
    valToPWM(JH_HW_NODE_VP37_PIN_QUANTITY_PWM, self->currentRPMSolenoid);
    return;
  }

  int32_t minQ10 = MIN_RPM_PERCENT_VALUE * RPM_PI_Q10_SCALE;
  int32_t maxQ10 = MAX_RPM_PERCENT_VALUE * RPM_PI_Q10_SCALE;

  if (!self->piInitialized) {
    self->rpmPercentValue =
        (int32_t)((self->currentRPMSolenoid * 100) / PWM_RESOLUTION);
    if (self->rpmPercentValue > MAX_RPM_PERCENT_VALUE) {
      self->rpmPercentValue = MAX_RPM_PERCENT_VALUE;
    } else if (self->rpmPercentValue < MIN_RPM_PERCENT_VALUE) {
      self->rpmPercentValue = MIN_RPM_PERCENT_VALUE;
    }
    self->rpmIntegratorQ10 = self->rpmPercentValue * RPM_PI_Q10_SCALE;
    self->piInitialized = true;
  }

  int32_t pTermQ10 = error * RPM_PI_KP_Q10;
  int32_t iDeltaQ10 =
      (int32_t)(((int64_t)error * RPM_PI_KI_Q10 * RPM_PI_UPDATE_INTERVAL_MS) /
                1000);
  int32_t uUnsatQ10 = self->rpmIntegratorQ10 + pTermQ10;
  bool atHigh = (uUnsatQ10 > maxQ10);
  bool atLow = (uUnsatQ10 < minQ10);

  if ((atHigh && error > 0) || (atLow && error < 0)) {
    iDeltaQ10 = 0;
  }

  int64_t nextIntegratorQ10 =
      (int64_t)self->rpmIntegratorQ10 + (int64_t)iDeltaQ10;
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

  valToPWM(JH_HW_NODE_VP37_PIN_QUANTITY_PWM, self->currentRPMSolenoid);

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
  bool coldEngine =
      ((int32_t)getGlobalValue(F_COOLANT_TEMP)) <= TEMP_COLD_ENGINE;
  if (coldEngine) {
    desiredRPM = COLD_RPM_VALUE;
  }
  bool regenActive = isDPFRegenerating();
  if (regenActive) {
    desiredRPM = REGEN_RPM_VALUE;
  }

  int32_t rpmForControl =
      (self->rpmFiltered > 0) ? self->rpmFiltered : RPM_getCurrentRPM(self);
  int32_t error = desiredRPM - rpmForControl;
  int32_t iPercent = self->rpmIntegratorQ10 / RPM_PI_Q10_SCALE;
  int32_t outPercent =
      (int32_t)((self->currentRPMSolenoid * 100) / PWM_RESOLUTION);
  int32_t pTermDeciPct = (s_lastPTermQ10 * 10) / RPM_PI_Q10_SCALE;
  int32_t iDeltaDeciPct = (s_lastIDeltaQ10 * 10) / RPM_PI_Q10_SCALE;
  unsigned long piStepAgoMs =
      (s_lastPiStepAtMs == 0) ? 0 : (now - s_lastPiStepAtMs);

  deb("rpmPI rpm:%d filt:%d des:%d err:%d out:%d cmd:%d i:%d vac:%d cyc:%d "
      "stepAgo:%lu p10:%d iD10:%d thr:%d regen:%d cold:%d",
      RPM_getCurrentRPM(self), self->rpmFiltered, desiredRPM, error,
      self->rpmPercentValue, outPercent, iPercent, self->vacuumReady ? 1 : 0,
      self->rpmCycle ? 1 : 0, piStepAgoMs, pTermDeciPct, iDeltaDeciPct,
      RPM_isEngineThrottlePressed(self) ? 1 : 0, regenActive ? 1 : 0,
      coldEngine ? 1 : 0);
#else
  deb("rpm:%d current:%d", RPM_getCurrentRPM(self), self->currentRPMSolenoid);
#endif
}
