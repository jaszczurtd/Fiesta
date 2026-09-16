// VP37 injection pump: lifecycle, demand and the control cycle. The cycle
// runs on core 1 under vp37StateMutex and calls into the other units of the
// module in the order the command is built: feedback, feedforward, the
// multipliers, the correction loop, the output.

#include "vp37_internal.h"
#include <math.h>
#include <string.h>

static void VP37_throttleCycle(VP37Pump *self);
static void VP37_writeQuantityPWM(VP37Pump *self, int32_t pwm);

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
  self->potentiometerDemand = 0;
  self->potentiometerCandidate = 0;
  self->potentiometerCandidateSinceMs = 0U;
  self->potentiometerDemandReady = false;
  self->calibrationDone = false;
  self->desiredAdjustometerTarget = -1;
  self->desiredAdjustometer = -1;
  self->currentAdjustometerPosition = -1;
  self->adjCommLostSince = 0;
  self->adjCommFailed = false;
  self->pidErr = 0;
  self->pwmFeedForward = VP37_PWM_FF_AT_MIN;
  self->feedForwardRiseBlend = 0.0f;
  self->feedForwardFallBlend = 0.0f;
  self->feedForwardMotion = 0.0f;
  self->pidCorrection = 0.0f;
  self->pidPositiveLimit = VP37_PID_CORR_LIMIT_POSITIVE_COLD;
  self->pwmValue = VP37_PWM_MIN;
  self->voltageCorrection = 1.0f;
  self->compensationInputVolts = NOMINAL_VOLTAGE;
  self->compensationVolts = NOMINAL_VOLTAGE;
  self->voltageReady = false;
  self->voltageFrozen = false;
  self->lastPWMval = -1;
  self->finalPWM = VP37_PWM_MIN;
  self->localVolts = 0.0f;
  self->previousLocalVolts = 0.0f;
  self->localVoltageScale = 1.0f;
  self->localVoltageReady = false;
  self->voltageOverRange = false;
  self->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  self->pidTf = VP37_PID_TF;
  self->throttleRampLastMs = hal_millis();
  self->lastAdjustometerStatus = ADJ_STATUS_SIGNAL_LOST;
  self->pidSaturatedHigh = false;
  self->integralHold = false;
  self->integralHoldEnterPending = false;
  self->integralHoldConfirmMs = VP37_INTEGRAL_HOLD_CONFIRM_MS;
  self->integralHoldReleasePending = false;
  self->integralHoldReleaseStartedMs = 0U;
  self->controlStarted = false;
  self->pidStarted = false;
  self->pidDtUs = 0U;
  self->controlSequence = 0U;
  self->controlDtUs = 0U;
  self->pidTerms = (hal_pid_terms_t){0};
  self->softFloorActive = false;
  self->pwmLimited = false;
  self->quantityAtRest = false;
  self->temperatureCorrection = 1.0f;
  self->temperatureCompensationWeight = 1.0f;
  self->currentObservationEnabled = true;
  self->cycleVoltageEnabled = true;
  self->cycleVoltageUsed = false;
  self->cycleSupplyValid = false;
  self->temperatureReady = false;
  self->cycleCurrentValid = false;
  self->cycleCurrentAmps = 0.0f;
  self->cycleCurrentVolts = 0.0f;
  self->cycleCurrentPwm = 0;
  self->cycleCurrentDrive = 0;
  self->cycleCurrentUs = 0U;
  (void)memset(&self->cycleResult, 0, sizeof(self->cycleResult));
  self->cycleResultStatus = HAL_NONE;
  self->cycleResultSequence = 0U;
  self->scanLastSequence = 0U;
  self->scanBlocks = 0U;
  self->scanGaps = 0U;
  self->scanFrameNs = 0U;
  self->scanRunning = false;
  self->driveResistance = VP37_DRIVE_REFERENCE_OHMS;
  self->driveCorrection = 1.0f;
  self->driveSamples = 0U;
  self->driveUpdatedMs = 0U;
  self->driveFirstSampleMs = 0U;
  self->driveResistanceReady = false;
  self->driveCompensationEnabled = true;
  self->driveCompensationUsed = false;
  self->thermalScale = 1.0f;
  self->thermalScaleReady = false;
  self->integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  self->integralDeadbandHz = (float)VP37_PID_DEADBAND;
  self->motionBoostUp = VP37_PWM_FF_MOTION_BOOST_DEFAULT;
  self->motionBoostDown = VP37_PWM_FF_DESCENT_BOOST;
  for (uint32_t i = 0U; i < COUNTOF(self->mapTrim); i++) {
    self->mapTrim[i] = 0.0f;
  }
  self->mapTrimApplied = 0.0f;
  self->mapTrimEnabled = false;
  self->integralHoldEntered = false;
  self->mapTrimTransfers = 0U;
  // The bench cap equals the bottom of the position profile, so it only ever
  // limits what a console command lowered.
#if ECU_FUNCTIONAL_TESTS_ENABLED
  self->pidIntegralOverride = VP37_BENCH_INTEGRAL_CAP_PWM;
#else
  self->pidIntegralOverride = 0.0f;
#endif

  if (self->adjustController == NULL) {
    self->adjustController = hal_pid_controller_create();
    if (self->adjustController == NULL) {
      derr("VP37 init failed: cannot create PID controller");
      return VP37_INIT_PID_CREATE_FAILED;
    }
  }

  VP37_setVP37PID(self, VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, false);
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

void VP37_stop(VP37Pump *self) {
  self->vp37Initialized = false;
  self->finalPWM = 0;
  self->lastPWMval = 0;
  valToPWM(PIO_VP37_RPM, 0);
  VP37_enableVP37(self, false);
}

bool VP37_isVP37Enabled(VP37Pump *self) {
  (void)self;
  return pcf8574_read(PCF8574_O_VP37_ENABLE);
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

  accel = hal_math_map_f32(
      accel, (float)VP37_PERCENT_MIN, (float)VP37_PERCENT_MAX,
      (float)VP37_ACCELERATION_MIN, (float)VP37_ACCELERATION_MAX);

  accel =
      hal_constrain(accel, (float)VP37_PERCENT_MIN, (float)VP37_PERCENT_MAX);
  self->lastThrottle = accel;
  const int32_t target = (int32_t)hal_math_map_f32(
      accel, VP37_PERCENT_MIN, VP37_PERCENT_MAX, (float)self->VP37_ADJUST_MIN,
      (float)self->VP37_ADJUST_MAX);
  if (target != self->desiredAdjustometerTarget) {
    self->targetChangedMs = hal_millis();
  }
  self->desiredAdjustometerTarget = target;
}

void VP37_setPotentiometerThrottle(VP37Pump *self, int32_t accel) {
  if (!self->calibrationDone) {
    VP37_setVP37Throttle(self, (float)accel);
    return;
  }

  const int32_t demand =
      hal_constrain(accel, VP37_PERCENT_MIN, VP37_PERCENT_MAX);
  if (!self->potentiometerDemandReady) {
    self->potentiometerDemand = demand;
    self->potentiometerCandidate = demand;
    self->potentiometerCandidateSinceMs = hal_millis();
    self->potentiometerDemandReady = true;
    VP37_setVP37Throttle(self, (float)demand);
    return;
  }

  const int32_t delta = demand - self->potentiometerDemand;
  const int32_t absoluteDelta = delta < 0 ? -delta : delta;
  if ((absoluteDelta == 0) || (absoluteDelta > 1)) {
    self->potentiometerCandidate = demand;
    self->potentiometerCandidateSinceMs = hal_millis();
    if (absoluteDelta > 1) {
      self->potentiometerDemand = demand;
    }
    VP37_setVP37Throttle(self, (float)self->potentiometerDemand);
    return;
  }

  if (self->potentiometerCandidate != demand) {
    self->potentiometerCandidate = demand;
    self->potentiometerCandidateSinceMs = hal_millis();
  } else if (hal_millis_deadline_expired(self->potentiometerCandidateSinceMs,
                                         VP37_POTENTIOMETER_STEP_CONFIRM_MS)) {
    self->potentiometerDemand = demand;
    VP37_setVP37Throttle(self, (float)demand);
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

void VP37_process(VP37Pump *self) {
  if (!self->vp37Initialized) {
    return;
  }
  if (!isfinite(self->pidTimeUpdate) || self->pidTimeUpdate < 1.0f ||
      self->pidTimeUpdate > 100.0f) {
    VP37_stop(self);
    return;
  }
  const uint32_t nowUs = hal_micros();
  const uint32_t periodUs = (uint32_t)(self->pidTimeUpdate * 1000.0f);
  if (self->controlStarted &&
      !hal_elapsed_u32(nowUs, self->controlLastUs, periodUs)) {
    return;
  }
  self->controlDtUs =
      self->controlStarted ? nowUs - self->controlLastUs : periodUs;
  self->controlLastUs = nowUs;
  self->controlStarted = true;
  self->controlSequence++;
  self->pidDtUs = 0U;
  (void)VP37_serviceCurrentScan(self);

  if (!VP37_updateAdjustometerPosition(self)) {
    if (hal_elapsed_u32(hal_millis(), self->adjCommLostSince,
                        VP37_ADJ_COMM_CUTOFF_MS)) {
      VP37_stop(self);
      derr("VP37 disabled: feedback communication timeout");
    }
  } else if (!self->feedbackFresh ||
             (self->lastAdjustometerStatus &
              (ADJ_STATUS_SIGNAL_LOST | ADJ_STATUS_BASELINE_PENDING)) != 0U) {
    VP37_stop(self);
    derr("VP37 disabled: invalid feedback status:%u fresh:%d",
         self->lastAdjustometerStatus, self->feedbackFresh);
  } else if ((int32_t)getGlobalValue(F_RPM) > RPM_MAX_EVER) {
    VP37_stop(self);
    derr("VP37 disabled: RPM too high");
  } else {
    self->pidDtUs = self->pidStarted ? nowUs - self->pidLastUs : periodUs;
    self->pidLastUs = nowUs;
    self->pidStarted = true;
    VP37_throttleCycle(self);
  }
#if ECU_FUNCTIONAL_TESTS_ENABLED
  VP37_traceRecord(self);
#endif
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

  const int32_t previousDesired = self->desiredAdjustometer;
  const float previousPosition = previousDesired < 0
                                     ? (float)self->desiredAdjustometerTarget
                                     : self->desiredPosition;
  const float dt = (float)self->pidDtUs * 0.000001f;
  const bool stationaryTarget =
      hal_millis_deadline_expired(self->targetChangedMs, VP37_TARGET_STABLE_MS);
  if (self->desiredAdjustometer < 0) {
    self->desiredPosition = (float)self->desiredAdjustometerTarget;
  } else {
    const float travel =
        (float)self->VP37_ADJUST_MAX - (float)self->VP37_ADJUST_MIN;
    const float delta =
        (float)self->desiredAdjustometerTarget - self->desiredPosition;
    const float upperStart =
        (float)self->VP37_ADJUST_MIN +
        travel * (VP37_DESIRED_UPPER_SLEW_START_PERCENT * 0.01f);
    float rate = delta > 0.0f && self->desiredPosition >= upperStart
                     ? VP37_DESIRED_UPPER_SLEW_PERCENT_PER_SECOND
                     : VP37_DESIRED_SLEW_PERCENT_PER_SECOND;
    if (delta > 0.0f && stationaryTarget) {
      rate = self->desiredPosition >= upperStart
                 ? VP37_STATIONARY_UPPER_SLEW_PERCENT_PER_SECOND
                 : VP37_STATIONARY_SLEW_PERCENT_PER_SECOND;
    }
    const float step = travel * (rate * 0.01f) * dt;
    self->desiredPosition += hal_constrain(delta, -step, step);
  }
  self->desiredAdjustometer = (int32_t)self->desiredPosition;
  self->pidErr = self->desiredAdjustometer - self->currentAdjustometerPosition;

  const float travel =
      (float)self->VP37_ADJUST_MAX - (float)self->VP37_ADJUST_MIN;
  const float upwardStep =
      travel * (VP37_PWM_FF_MOTION_REFERENCE_RATE * 0.01f) * dt;
  const float maxRise =
      VP37_DESIRED_SLEW_PERCENT_PER_SECOND / VP37_PWM_FF_MOTION_REFERENCE_RATE;
  const float rise =
      (stationaryTarget ? VP37_STATIONARY_MOTION_WEIGHT : 1.0f) *
      (upwardStep > 0.0f
           ? hal_constrain((self->desiredPosition - previousPosition) /
                               upwardStep,
                           0.0f, maxRise)
           : 0.0f);
  self->feedForwardRiseBlend += (rise - self->feedForwardRiseBlend) * dt /
                                (VP37_PWM_FF_MOTION_FILTER_S + dt);
  const float fall =
      (stationaryTarget ? VP37_STATIONARY_MOTION_WEIGHT : 1.0f) *
      (upwardStep > 0.0f
           ? hal_constrain((previousPosition - self->desiredPosition) /
                               upwardStep,
                           0.0f, maxRise)
           : 0.0f);
  self->feedForwardFallBlend += (fall - self->feedForwardFallBlend) * dt /
                                (VP37_PWM_FF_MOTION_FILTER_S + dt);

  // FF and PID share the warm reference domain; temperature scales the sum.
  self->pwmFeedForward = VP37_feedForward(self, self->desiredAdjustometer);

  // Keep correction and integral authority in the same reference domain.
  // Applying the measured temperature here as well would compensate twice.
  self->pidPositiveLimit = VP37_PID_CORR_LIMIT_POSITIVE_COLD;
  if (self->adjCommLostSince == 0U) {
    self->pidPositiveLimit = VP37_computePositiveCorrectionLimit(
        VP37_PWM_REFERENCE_TEMP_C, self->lastAdjustometerStatus,
        self->pwmFeedForward);
  }

  self->pidIntegralLimit = VP37_integralLimit(self);
  const float ki = hal_pid_controller_get_ki(self->adjustController);
  const float maxIntegral = ki > 0.0f ? self->pidIntegralLimit / ki : 0.0f;
  hal_pid_controller_set_max_integral(self->adjustController, maxIntegral);
  self->lastVolts = getGlobalValue(F_VOLTS);
  self->localVolts = getLocalSystemSupplyVoltage();
  if (self->lastVolts < VP37_MIN_COMPENSATION_VOLTAGE) {
    self->lastVolts = VP37_MIN_COMPENSATION_VOLTAGE;
  }
  self->lastFuelTemp = getGlobalValue(F_FUEL_TEMP);
  VP37_updateVoltageCorrection(self, dt);
  VP37_updateTemperatureCorrection(self, dt);
  VP37_updateDriveCorrection(self, dt);
  VP37_updateThermalScale(self, dt);
  const float outputScale = self->voltageCorrection * self->thermalScale;

  // Finish the commanded descent before releasing the spring-return actuator.
  // Neither feedforward nor a retained integral may energize it at zero demand.
  self->quantityAtRest = self->lastThrottle <= (float)VP37_PERCENT_MIN &&
                         self->desiredPosition <= (float)self->VP37_ADJUST_MIN;
  if (self->quantityAtRest) {
    hal_pid_controller_reset(self->adjustController);
    self->pidTerms = (hal_pid_terms_t){0};
    self->pwmFeedForward = 0.0f;
    self->feedForwardRiseBlend = 0.0f;
    self->feedForwardFallBlend = 0.0f;
    self->feedForwardMotion = 0.0f;
    self->pidCorrection = 0.0f;
    self->pwmValue = 0.0f;
    self->pidNegativeLimit = 0.0f;
    self->pidUpperLimit = 0.0f;
    self->pidSaturatedHigh = false;
    self->integralHold = false;
    self->integralHoldEnterPending = false;
    self->integralHoldReleasePending = false;
    self->softFloorActive = false;
    self->pwmLimited = false;
    VP37_writeQuantityPWM(self, 0);
    return;
  }

  // Express every actuator limit in the PID correction domain before stepping.
  float lowerCommand =
      fmaxf((float)VP37_PWM_MIN / outputScale,
            self->pwmFeedForward - VP37_PID_CORR_LIMIT_NEGATIVE);
  const float upperCommand = (float)VP37_PWM_MAX / outputScale;
  self->softFloorActive = false;
  if (self->currentAdjustometerPosition < self->desiredAdjustometer) {
    const float floor =
        self->pwmFeedForward - (float)VP37_PWM_FF_SOFT_FLOOR_MARGIN;
    if (floor > lowerCommand) {
      lowerCommand = floor;
      self->softFloorActive = true;
    }
  }
  self->pidNegativeLimit =
      fmaxf(-VP37_PID_CORR_LIMIT_NEGATIVE, lowerCommand - self->pwmFeedForward);
  self->pidUpperLimit =
      fminf(self->pidPositiveLimit, upperCommand - self->pwmFeedForward);
  hal_pid_controller_set_output_limits(
      self->adjustController, self->pidNegativeLimit, self->pidUpperLimit);
  // Ramp tracking lag must not build a new holding trim in either direction.
  // Allow an existing trim to unwind, including a reversal before zero release.
  const bool rampWindup =
      previousDesired >= 0 && self->desiredAdjustometer != previousDesired &&
      self->pidTerms.integral * ki * (float)self->pidErr >= 0.0f;
  const bool targetSettled =
      stationaryTarget &&
      (self->desiredAdjustometer == self->desiredAdjustometerTarget);
  self->integralDeadbandHz = VP37_integralDeadband(self);
  VP37_updateIntegralHold(self, targetSettled);
  // Supply changes are scaled out of the command before it reaches the
  // actuator, so they never freeze integration.
  const bool freezeIntegral = rampWindup || self->integralHold;
  const float integralDeadband =
      freezeIntegral ? fabsf((float)self->pidErr) : self->integralDeadbandHz;
  const hal_status_t pidStatus =
      hal_pid_controller_step_ex(self->adjustController, (float)self->pidErr,
                                 (float)self->currentAdjustometerPosition, dt,
                                 integralDeadband, &self->pidTerms);
  if (pidStatus != HAL_OK) {
    VP37_stop(self);
    derr("VP37 PID step failed: %s", hal_status_to_string(pidStatus));
    return;
  }
  self->pidCorrection = self->pidTerms.output;
  VP37_transferIntegralToMapTrim(self);
  self->pidSaturatedHigh = self->pidTerms.saturated_high;
  self->pwmValue = self->pwmFeedForward + self->pidCorrection;
  const float compensatedPWM = self->pwmValue * outputScale;
  self->finalPWM = (int32_t)compensatedPWM;
  self->pwmLimited =
      (self->finalPWM < VP37_PWM_MIN) || (self->finalPWM > VP37_PWM_MAX);
  self->finalPWM = hal_constrain(self->finalPWM, VP37_PWM_MIN, VP37_PWM_MAX);
  self->softFloorActive = self->softFloorActive && self->pidTerms.saturated_low;

  VP37_writeQuantityPWM(self, self->finalPWM);
}

static void VP37_writeQuantityPWM(VP37Pump *self, int32_t pwm) {
  self->finalPWM = pwm;
  if (self->lastPWMval != pwm) {
    self->lastPWMval = pwm;
    valToPWM(PIO_VP37_RPM, pwm);
  }
}
