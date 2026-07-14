#include "vp37.h"
#include <hal/hal_soft_timer.h>

/**
 * @brief Run the VP37 calibration sweep and capture Adjustometer limits.
 * @param self VP37 controller instance to calibrate.
 * @return True when both end positions settle and form a valid range.
 * @note This calibrates the project-local N146/G149-like quantity-feedback
 * range, not an OEM mg/stroke model.
 */
static bool VP37_makeCalibration(VP37Pump *self);

/**
 * @brief Refresh cached Adjustometer position and telemetry values.
 * @param self VP37 controller instance to update.
 * @return None.
 * @note The refreshed values are a project-local G149-like quantity feedback
 * plus G81-like fuel temperature and supply-voltage telemetry.
 */
static void VP37_updateAdjustometerPosition(VP37Pump *self);

float VP37_computePositiveCorrectionLimit(float fuelTempC,
                                          uint8_t adjustometerStatus,
                                          float pwmFeedForward) {
  const uint8_t invalidTemperatureStatus = ADJ_STATUS_SIGNAL_LOST |
                                           ADJ_STATUS_FUEL_TEMP_BROKEN |
                                           ADJ_STATUS_BASELINE_PENDING;

  if ((adjustometerStatus & invalidTemperatureStatus) != 0U ||
      fuelTempC != fuelTempC || fuelTempC > VP37_THERMAL_TEMP_VALID_MAX_C ||
      fuelTempC <= VP37_THERMAL_REFERENCE_TEMP_C) {
    return VP37_PID_CORR_LIMIT_POSITIVE_COLD;
  }

  const float coldMaximumCommand =
      pwmFeedForward + VP37_PID_CORR_LIMIT_POSITIVE_COLD;
  const float thermalFactor =
      1.0f + VP37_COPPER_TEMP_COEFFICIENT *
                 (fuelTempC - VP37_THERMAL_REFERENCE_TEMP_C);
  float positiveLimit = coldMaximumCommand * thermalFactor - pwmFeedForward;

  return hal_constrain(positiveLimit, VP37_PID_CORR_LIMIT_POSITIVE_COLD,
                       VP37_PID_CORR_LIMIT_POSITIVE_MAX);
}

VP37InitStatus VP37_init(VP37Pump *self) {
  if (self->vp37Initialized) {
    return VP37_INIT_ALREADY_INITIALIZED;
  }

  if (!waitForAdjustometerBaseline()) {
    derr_limited("VP37 init baseline",
                 "VP37 adjustometer baseline not ready, cannot initialize");
    return VP37_INIT_BASELINE_NOT_READY;
  }

  self->lastThrottle = -1;
  self->calibrationDone = false;
  self->desiredAdjustometerTarget = -1;
  self->desiredAdjustometer = -1;
  self->currentAdjustometerPosition = -1;
  self->adjCommLostSince = 0;
  self->pidErr = 0;
  self->pwmFeedForward = VP37_PWM_FF_AT_MIN;
  self->pidCorrection = 0.0f;
  self->pidPositiveLimit = VP37_PID_CORR_LIMIT_POSITIVE_COLD;
  self->pwmValue = VP37_PWM_MIN;
  self->voltageCorrection = 0;
  self->lastPWMval = -1;
  self->finalPWM = VP37_PWM_MIN;
  self->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  self->pidTf = VP37_PID_TF;
  self->throttleRampLastMs = hal_millis();
  self->lastAdjustometerStatus = ADJ_STATUS_SIGNAL_LOST;
  self->pidSaturatedHigh = false;

  if (self->adjustController == NULL) {
    self->adjustController = hal_pid_controller_create();
    if (self->adjustController == NULL) {
      derr("VP37 init failed: cannot create PID controller");
      return VP37_INIT_PID_CREATE_FAILED;
    }
  }

  hal_pid_controller_set_kp(self->adjustController, VP37_PID_KP);
  hal_pid_controller_set_ki(self->adjustController, VP37_PID_KI);
  hal_pid_controller_set_kd(self->adjustController, VP37_PID_KD);
  hal_pid_controller_set_tf(self->adjustController, self->pidTf);
  hal_pid_controller_set_max_integral(self->adjustController,
                                      VP37_PID_MAX_INTEGRAL);

  valToPWM(PIO_VP37_ANGLE, 0);

  if (!VP37_makeCalibration(self)) {
    VP37_updateAdjustometerPosition(self);
    VP37_enableVP37(self, false);
    return VP37_INIT_CALIBRATION_FAILED;
  }
  VP37_updateAdjustometerPosition(self);
  self->desiredAdjustometerTarget = -1;
  self->desiredAdjustometer = -1;

  VP37_enableVP37(self, self->calibrationDone);

  self->vp37Initialized = true;
  return VP37_INIT_OK;
}

void VP37_enableVP37(VP37Pump *self, bool enable) {
  (void)self;
  pcf8574_write(PCF8574_O_VP37_ENABLE, enable);
  deb("vp37 enabled: %d", VP37_isVP37Enabled(self));
}

bool VP37_isVP37Enabled(VP37Pump *self) {
  (void)self;
  return pcf8574_read(PCF8574_O_VP37_ENABLE);
}

/**
 * @brief Get the PWM value used during maximum-range calibration.
 * @param self VP37 controller instance using the calibration path.
 * @return PWM command used for calibration.
 */
static int32_t VP37_getMaxAdjustometerPWMVal(VP37Pump *self) {
  (void)self;
  return hal_map(VP37_CALIBRATION_MAX_PERCENTAGE, 0, 100, 0, PWM_RESOLUTION);
}

/**
 * @brief Read the latest Adjustometer pulse value for VP37 control.
 * @return Adjustometer pulse value in Hz, or -1 on communication failure.
 * @note This is a project-local G149-like raw quantity-feedback signal, not a
 * direct OEM quantity estimate.
 */
int32_t VP37_getAdjustometer(void) {
  adjustometer_reading_t reading;
  getVP37Adjustometer(&reading);
  if (!reading.commOk) {
    return -1;
  }
  return reading.pulseHz;
}

static void VP37_updateAdjustometerPosition(VP37Pump *self) {
  adjustometer_reading_t reading;
  getVP37Adjustometer(&reading);
  if (reading.commOk) {
    self->currentAdjustometerPosition = reading.pulseHz;
    self->adjCommLostSince = 0;
    self->lastAdjustometerStatus = reading.status;

    setGlobalValue(F_FUEL_TEMP, reading.fuelTempC);
    setGlobalValue(F_VOLTS, reading.voltageRaw * 0.1f);
  } else {
    if (self->adjCommLostSince == 0) {
      self->adjCommLostSince = hal_millis();
    }
    derr_limited("VP37 adj comm",
                 "Failed to read adjustometer for VP37 control");
  }
}

/**
 * @brief Wait until a calibration endpoint is mechanically stable.
 * @param self VP37 controller instance storing diagnostic samples.
 * @param settledValue Output receiving a trimmed mean of the stable window.
 * @return True when a stable valid window is found before timeout.
 */
static bool VP37_waitForCalibrationSettle(VP37Pump *self,
                                          int32_t *settledValue) {
  int32_t samples[VP37_CALIBRATION_STABLE_SAMPLES] = {0};
  uint32_t sampleCount = 0U;
  uint32_t nextSample = 0U;
  const uint32_t startMs = hal_millis();

  while ((hal_millis() - startMs) < VP37_CALIBRATION_TIMEOUT_MS) {
    hal_delay_ms(VP37_CALIBRATION_SAMPLE_INTERVAL_MS);
    watchdog_feed();

    adjustometer_reading_t reading;
    getVP37Adjustometer(&reading);
    if (!reading.commOk ||
        (reading.status &
         (ADJ_STATUS_SIGNAL_LOST | ADJ_STATUS_BASELINE_PENDING)) != 0U) {
      sampleCount = 0U;
      nextSample = 0U;
      continue;
    }

    samples[nextSample] = reading.pulseHz;
    nextSample = (nextSample + 1U) % VP37_CALIBRATION_STABLE_SAMPLES;
    if (sampleCount < VP37_CALIBRATION_STABLE_SAMPLES) {
      sampleCount++;
    }

    if ((hal_millis() - startMs) < VP37_CALIBRATION_MIN_SETTLE_MS ||
        sampleCount < VP37_CALIBRATION_STABLE_SAMPLES) {
      continue;
    }

    int32_t minValue = samples[0];
    int32_t maxValue = samples[0];
    int32_t sum = samples[0];
    for (uint32_t i = 1U; i < VP37_CALIBRATION_STABLE_SAMPLES; i++) {
      if (samples[i] < minValue) {
        minValue = samples[i];
      }
      if (samples[i] > maxValue) {
        maxValue = samples[i];
      }
      sum += samples[i];
    }

    if ((maxValue - minValue) <= VP37_CALIBRATION_STABLE_SPAN_HZ) {
      sum -= minValue;
      sum -= maxValue;
      *settledValue = sum / (int32_t)(VP37_CALIBRATION_STABLE_SAMPLES - 2U);

      for (uint32_t i = 0U; i < STABILITY_ADJUSTOMETER_TAB_SIZE &&
                            i < VP37_CALIBRATION_STABLE_SAMPLES;
           i++) {
        self->adjustStabilityTable[i] = samples[i];
      }
      return true;
    }
  }

  return false;
}

static bool VP37_makeCalibration(VP37Pump *self) {
  self->VP37_ADJUST_MAX = self->VP37_ADJUST_MIDDLE = self->VP37_ADJUST_MIN =
      self->VP37_OPERATE_MAX = -1;

  // Capture the natural/resting endpoint first.  Measuring MIN after a strong
  // MAX pulse biases it with actuator hysteresis and oscillator thermal drift.
  valToPWM(PIO_VP37_RPM, 0);
  bool minSettled = VP37_waitForCalibrationSettle(self, &self->VP37_ADJUST_MIN);

  bool maxSettled = false;
  if (minSettled) {
    valToPWM(PIO_VP37_RPM, VP37_getMaxAdjustometerPWMVal(self));
    maxSettled = VP37_waitForCalibrationSettle(self, &self->VP37_ADJUST_MAX);
  }
  valToPWM(PIO_VP37_RPM, 0);

  if (!maxSettled || !minSettled) {
    self->calibrationDone = false;
    derr("VP37 calibration timeout: maxSettled=%d minSettled=%d MAX=%d MIN=%d",
         maxSettled, minSettled, self->VP37_ADJUST_MAX, self->VP37_ADJUST_MIN);
    return false;
  }

  self->VP37_ADJUST_MIDDLE =
      ((self->VP37_ADJUST_MAX - self->VP37_ADJUST_MIN) / 2) +
      self->VP37_ADJUST_MIN;
  const int32_t calibrationTravel =
      self->VP37_ADJUST_MAX - self->VP37_ADJUST_MIN;
  self->calibrationDone = calibrationTravel >= VP37_CALIBRATION_MIN_TRAVEL_HZ &&
                          self->VP37_ADJUST_MIDDLE > 0;
  if (!self->calibrationDone) {
    derr(
        "VP37 calibration range invalid: MIN=%d MAX=%d travel=%d (required=%d)",
        self->VP37_ADJUST_MIN, self->VP37_ADJUST_MAX, calibrationTravel,
        VP37_CALIBRATION_MIN_TRAVEL_HZ);
  }

  hal_pid_controller_set_output_limits(self->adjustController,
                                       -VP37_PID_CORR_LIMIT_NEGATIVE,
                                       VP37_PID_CORR_LIMIT_POSITIVE_COLD);
  hal_pid_controller_reset(self->adjustController);
  deb("VP37 calibration: MIN=%d MIDDLE=%d MAX=%d OPERATE_MAX=%d",
      self->VP37_ADJUST_MIN, self->VP37_ADJUST_MIDDLE, self->VP37_ADJUST_MAX,
      self->VP37_OPERATE_MAX);
  return self->calibrationDone;
}

/**
 * @brief Execute the inner VP37 quantity-control cycle.
 * @param self VP37 controller instance to update.
 * @return None.
 * @note The current input still comes from legacy throttle-named driver demand,
 *       but the controlled plant is the project-local N146/G149-like inner
 * loop.
 */
static void VP37_throttleCycle(VP37Pump *self) {
  if (self->desiredAdjustometerTarget < 0) {
    return;
  }

  hal_pid_controller_update_time(self->adjustController, self->pidTimeUpdate);

  // First-run snap: when no slewed setpoint exists yet, jump to target
  // immediately so PID has a sensible reference from the first cycle.
  if (self->desiredAdjustometer < 0) {
    self->desiredAdjustometer = self->desiredAdjustometerTarget;
  } else {
    // Slew-rate limit on the setpoint fed to PID. Prevents huge instantaneous
    // jumps in pwm_ff (and therefore PWM) when throttle changes abruptly,
    // which would otherwise slam the actuator into its mechanical stops.
    int32_t delta = self->desiredAdjustometerTarget - self->desiredAdjustometer;
    if (delta > VP37_DESIRED_SLEW_PER_CYCLE) {
      delta = VP37_DESIRED_SLEW_PER_CYCLE;
    } else if (delta < -VP37_DESIRED_SLEW_PER_CYCLE_DOWN) {
      delta = -VP37_DESIRED_SLEW_PER_CYCLE_DOWN;
    }
    self->desiredAdjustometer += delta;
  }

  // Clamp measured position to the calibrated range.  When the adjustometer
  // physically exceeds the calibrated max (mechanical saturation at the upper
  // end of travel), clamping prevents inflated error and integral windup
  // from a region the controller cannot influence.
  int32_t clampedAdj =
      hal_constrain(self->currentAdjustometerPosition, self->VP37_ADJUST_MIN,
                    self->VP37_ADJUST_MAX);
  self->pidErr = self->desiredAdjustometer - clampedAdj;

  // Deadband: suppress small errors to prevent integral windup and let
  // the system settle when already close to the setpoint.
  float pidInput = (float)self->pidErr;
  if (pidInput > -VP37_PID_DEADBAND && pidInput < VP37_PID_DEADBAND) {
    pidInput = 0.0f;
  }

  // Feedforward carries the expected steady-state command at the current
  // requested position.  It is computed before the PID limit because the
  // thermal headroom scales the complete cold saturated command (FF + 220).
  self->pwmFeedForward =
      mapfloat((float)self->desiredAdjustometer, (float)self->VP37_ADJUST_MIN,
               (float)self->VP37_ADJUST_MAX, (float)VP37_PWM_FF_AT_MIN,
               (float)VP37_PWM_FF_AT_MAX);

  // Preserve the original +/-220 authority at room temperature.  When the
  // pump is warm and its temperature reading is valid, expand only the
  // positive side by the copper-resistance estimate.  The integral storage is
  // expanded in the same proportion so the extra authority remains available
  // after the proportional error has fallen.
  self->pidPositiveLimit = VP37_PID_CORR_LIMIT_POSITIVE_COLD;
  if (self->adjCommLostSince == 0U) {
    self->pidPositiveLimit = VP37_computePositiveCorrectionLimit(
        getGlobalValue(F_FUEL_TEMP), self->lastAdjustometerStatus,
        self->pwmFeedForward);
  }

  const float positiveLimitRange =
      VP37_PID_CORR_LIMIT_POSITIVE_MAX - VP37_PID_CORR_LIMIT_POSITIVE_COLD;
  float warmRatio = 0.0f;
  if (positiveLimitRange > 0.0f) {
    warmRatio = (self->pidPositiveLimit - VP37_PID_CORR_LIMIT_POSITIVE_COLD) /
                positiveLimitRange;
  }
  warmRatio = hal_constrain(warmRatio, 0.0f, 1.0f);
  const float maxIntegral = (float)VP37_PID_MAX_INTEGRAL +
                            warmRatio * ((float)VP37_PID_MAX_INTEGRAL_WARM -
                                         (float)VP37_PID_MAX_INTEGRAL);
  hal_pid_controller_set_max_integral(self->adjustController, maxIntegral);
  hal_pid_controller_set_output_limits(self->adjustController,
                                       -VP37_PID_CORR_LIMIT_NEGATIVE,
                                       self->pidPositiveLimit);

  self->pidCorrection =
      hal_pid_controller_update(self->adjustController, pidInput);
  self->pidSaturatedHigh =
      pidInput > 0.0f && self->pidCorrection >= (self->pidPositiveLimit - 0.5f);

  self->pwmValue = self->pwmFeedForward + self->pidCorrection;

  // Soft floor on the command (nominal-voltage units), BEFORE voltage
  // compensation. While ramping up to a new (higher) target the slewed
  // setpoint is below the target, so pwmFF and the resulting pwmValue
  // can be too low to actually drive the actuator there in reasonable
  // time. Compute the FF that would correspond to the final target and
  // ensure pwmValue stays at least (FF_target - margin). Only enforce
  // when the actuator is still below the target (climb phase); during
  // overshoot/recovery the controller must be free to lower PWM.
  if (self->desiredAdjustometerTarget > 0 &&
      self->currentAdjustometerPosition < self->desiredAdjustometerTarget) {
    float pwmFFTarget =
        mapfloat((float)self->desiredAdjustometerTarget,
                 (float)self->VP37_ADJUST_MIN, (float)self->VP37_ADJUST_MAX,
                 (float)VP37_PWM_FF_AT_MIN, (float)VP37_PWM_FF_AT_MAX);
    float softFloor = pwmFFTarget - (float)VP37_PWM_FF_SOFT_FLOOR_MARGIN;
    if (self->pwmValue < softFloor) {
      self->pwmValue = softFloor;
    }
  }

  // Voltage compensation: scale PWM inversely with battery voltage so the
  // average coil current (and therefore actuator force) stays consistent.
  // Clamp Vbat to a minimum so a noisy/missing sensor cannot inflate PWM.
  float volts = getGlobalValue(F_VOLTS);
  if (volts < VP37_MIN_COMPENSATION_VOLTAGE) {
    volts = VP37_MIN_COMPENSATION_VOLTAGE;
  }
  self->lastVolts = volts;
  self->voltageCorrection = NOMINAL_VOLTAGE / self->lastVolts;
  self->finalPWM = self->pwmValue * self->voltageCorrection;

  self->finalPWM = hal_constrain(self->finalPWM, (int32_t)VP37_PWM_MIN,
                                 (int32_t)(VP37_PWM_MAX));

  if (self->lastPWMval != self->finalPWM) {
    self->lastPWMval = self->finalPWM;
    valToPWM(PIO_VP37_RPM, self->finalPWM);
  }
}

/**
 * @brief Set the current timing-actuator command for the VP37 pump.
 * @param self VP37 controller instance issuing the command.
 * @param angle Requested timing angle in the 0..100 range.
 * @return None.
 * @note This is closest to the N108 actuator side of SOI control. G80/G28
 * closed-loop timing feedback is not implemented here yet.
 */
void VP37_setInjectionTiming(VP37Pump *self, int32_t angle) {
  (void)self;
  angle = hal_constrain(angle, 0, 100);
  valToPWM(PIO_VP37_ANGLE,
           hal_map(angle, 0, 100, TIMING_PWM_MIN, TIMING_PWM_MAX));
}

/**
 * @brief Convert legacy driver-demand input into a VP37 quantity-position
 * target.
 * @param self VP37 controller instance to update.
 * @param accel Accelerator / driver-demand input in percentage-like units.
 * @return None.
 * @note Despite the legacy name, this maps G79/G185-like driver demand into the
 *       project-local N146/G149-like inner-loop target.
 */
void VP37_setVP37Throttle(VP37Pump *self, float accel) {
  if (!self->calibrationDone) {
    derr_limited("VP37 calibration", "Calibration not done!");
    return;
  }

  accel = mapfloat(accel, (float)VP37_PERCENT_MIN, (float)VP37_PERCENT_MAX,
                   (float)VP37_ACCELERATION_MIN, (float)VP37_ACCELERATION_MAX);

  accel =
      hal_constrain(accel, (float)VP37_PERCENT_MIN, (float)VP37_PERCENT_MAX);
  self->lastThrottle = accel;
  self->desiredAdjustometerTarget = (int32_t)mapfloat(
      accel, VP37_PERCENT_MIN, VP37_PERCENT_MAX, (float)self->VP37_ADJUST_MIN,
      (float)self->VP37_ADJUST_MAX);
}

void VP37_setVP37PID(VP37Pump *self, float kp, float ki, float kd,
                     bool shouldTriggerReset) {
  hal_pid_controller_set_kp(self->adjustController, kp);
  hal_pid_controller_set_ki(self->adjustController, ki);
  hal_pid_controller_set_kd(self->adjustController, kd);

  if (shouldTriggerReset) {
    hal_pid_controller_reset(self->adjustController);
    self->lastPWMval = -1;
    self->finalPWM = VP37_PWM_MIN;
  }
}

void VP37_getVP37PIDValues(VP37Pump *self, float *kp, float *ki, float *kd) {
  if (kp != NULL) {
    *kp = hal_pid_controller_get_kp(self->adjustController);
  }
  if (ki != NULL) {
    *ki = hal_pid_controller_get_ki(self->adjustController);
  }
  if (kd != NULL) {
    *kd = hal_pid_controller_get_kd(self->adjustController);
  }
}

float VP37_getVP37PIDTimeUpdate(VP37Pump *self) { return self->pidTimeUpdate; }

void VP37_process(VP37Pump *self) {
  if (self->vp37Initialized) {
    VP37_updateAdjustometerPosition(self);

    if (self->adjCommLostSince != 0 &&
        (hal_millis() - self->adjCommLostSince) >=
            (VP37_ADJ_COMM_CUTOFF_S * SECOND)) {
      self->vp37Initialized = false;
      VP37_enableVP37(self, false);
      derr("VP37 disabled: adjustometer comm lost for %u s",
           VP37_ADJ_COMM_CUTOFF_S);
      return;
    }

    // if((self->VP37_ADJUST_MAX <= 0 || self->VP37_ADJUST_MIDDLE <= 0 ||
    // self->VP37_ADJUST_MIN <= 0) &&
    //    self->currentAdjustometerPosition > MIN_ADJUSTOMETER_VAL) {
    //   VP37_makeCalibration(self);
    //   VP37_updateAdjustometerPosition(self);
    //   self->desiredAdjustometer = self->currentAdjustometerPosition;
    // }

    int32_t rpm = (int32_t)getGlobalValue(F_RPM);
    if (rpm > RPM_MAX_EVER) {
      self->vp37Initialized = false;
      VP37_enableVP37(self, false);
      derr("RPM was too high! (%d)", rpm);
      return;
    }

    VP37_throttleCycle(self);
  }
}

static uint32_t lastPeriodicLogMs = 0;

void VP37_showDebug(VP37Pump *self) {

  uint32_t now = hal_millis();
  if (now - lastPeriodicLogMs >= VP37_DEBUG_UPDATE) {
    lastPeriodicLogMs = now;

    adjustometer_reading_t telemetry;
    const bool extendedFresh = getVP37AdjustometerExtendedTelemetry(&telemetry);

    deb("VP37 thr:%.1f des:%d adj:%d V:%.1f t:%.1fC pwm:%d err:%d "
        "%.2f/%.2f/%.2f min:%d max:%d ff:%.1f corr:%.1f lim+:%.1f sat+:%d "
        "nom:%.1f",
        self->lastThrottle, self->desiredAdjustometer,
        self->currentAdjustometerPosition, getGlobalValue(F_VOLTS),
        getGlobalValue(F_FUEL_TEMP), self->finalPWM, self->pidErr,
        hal_pid_controller_get_kp(self->adjustController),
        hal_pid_controller_get_ki(self->adjustController),
        hal_pid_controller_get_kd(self->adjustController),
        self->VP37_ADJUST_MIN, self->VP37_ADJUST_MAX, self->pwmFeedForward,
        self->pidCorrection, self->pidPositiveLimit, self->pidSaturatedHigh,
        self->pwmValue);

    deb("VP37 ADJ p:%d f:%luHz d:%ld v:%u ft:%u tc:%.1f s:%u bl:%lu ext:%d "
        "fl:0x%02x",
        telemetry.pulseHz, (unsigned long)telemetry.signalHz,
        (long)telemetry.signedDeltaHz, (unsigned int)telemetry.voltageRaw,
        (unsigned int)telemetry.fuelTempC,
        (double)telemetry.chipTempDeciC * 0.1, (unsigned int)telemetry.status,
        (unsigned long)telemetry.baselineHz, extendedFresh,
        (unsigned int)telemetry.extendedFlags);
  }
}
