// VP37 injection pump: lifecycle, demand and the control cycle. The cycle
// runs on core 1 under vp37StateMutex and calls into the other units of the
// module in the order the command is built: feedback, feedforward, the
// multipliers, the correction loop, the output.

#include "vp37_internal.h"
#include <math.h>
#include <string.h>

/** @brief What one control step carries between its stages. */
typedef struct {
  float dt;              /**< Control period [s]. */
  bool stationaryTarget; /**< The target has stood for VP37_TARGET_STABLE_MS. */
  int32_t
      previousDesired; /**< Desired position before this step, -1 at start. */
  float previousPosition; /**< Slewed position before this step. */
  float ki;               /**< Integral gain in force this step. */
  float outputScale;      /**< Supply multiplier times thermal multiplier. */
} VP37Cycle;

static void VP37_positionCycle(VP37Pump *self);
static void VP37_beginCycle(const VP37Pump *self, VP37Cycle *cycle);
static void VP37_rampDemand(VP37Pump *self, const VP37Cycle *cycle);
static void VP37_blendMotion(VP37Pump *self, const VP37Cycle *cycle);
static void VP37_updateAuthority(VP37Pump *self, VP37Cycle *cycle);
static void VP37_updateMultipliers(VP37Pump *self, VP37Cycle *cycle);
static bool VP37_releaseAtRest(VP37Pump *self);
static void VP37_boundCorrection(VP37Pump *self, const VP37Cycle *cycle);
static bool VP37_stepCorrection(VP37Pump *self, const VP37Cycle *cycle);
static void VP37_composeCommand(VP37Pump *self, const VP37Cycle *cycle);
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

  self->demand.requestedPercent = -1;
  self->currentControl.enabled = true;
  VP37_resetCurrentControl(self);
  self->feedback.calibrationDone = false;
  self->demand.target = -1;
  self->demand.desired = -1;
  self->feedback.position = -1;
  self->feedback.commLostSince = 0;
  self->feedback.commFailed = false;
  self->pid.error = 0;
  self->feedforward.pwm = VP37_PWM_FF_AT_MIN;
  self->feedforward.riseBlend = 0.0f;
  self->feedforward.fallBlend = 0.0f;
  self->feedforward.motion = 0.0f;
  self->pid.correction = 0.0f;
  self->pid.positiveLimit = VP37_PID_CORR_LIMIT_POSITIVE_COLD;
  self->output.pwmValue = VP37_PWM_MIN;
  self->supply.correction = 1.0f;
  self->supply.inputVolts = NOMINAL_VOLTAGE;
  self->supply.heldVolts = NOMINAL_VOLTAGE;
  self->supply.ready = false;
  self->supply.frozen = false;
  self->output.lastPWMval = -1;
  self->output.finalPWM = VP37_PWM_MIN;
  self->supply.localVolts = 0.0f;
  self->supply.previousLocalVolts = 0.0f;
  self->supply.localScale = 1.0f;
  self->supply.localReady = false;
  self->supply.overRange = false;
  self->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  self->pid.tf = VP37_PID_TF;
  self->pid.topKd = VP37_PID_TOP_KD;
  self->pid.topDBlend = 0.0f;
  self->feedback.lastStatus = ADJ_STATUS_SIGNAL_LOST;
  self->pid.saturatedHigh = false;
  self->pid.integralHold = false;
  self->pid.integralHoldEnterPending = false;
  self->pid.integralHoldConfirmMs = VP37_INTEGRAL_HOLD_CONFIRM_MS;
  self->pid.integralHoldReleasePending = false;
  self->pid.integralHoldReleaseStartedMs = 0U;
  self->controlStarted = false;
  self->pidStarted = false;
  self->pidDtUs = 0U;
  self->controlSequence = 0U;
  self->controlDtUs = 0U;
  self->controlExecUs = 0U;
  self->pid.terms = (hal_pid_terms_t){0};
  self->pid.softFloorActive = false;
  self->output.pwmLimited = false;
  self->demand.atRest = false;
  self->thermal.temperatureCorrection = 1.0f;
  self->thermal.temperatureCompensationWeight = 1.0f;
  self->thermal.observationEnabled = true;
  self->supply.cycleEnabled = true;
  self->supply.cycleUsed = false;
  self->supply.cycleValid = false;
  self->supply.cycleAgeUs = 0U;
  self->supply.predictionSampleUs = 0U;
  self->supply.predictionSampleVolts = 0.0f;
  self->supply.voltageSlope = 0.0f;
  self->supply.predictionVolts = 0.0f;
  self->supply.predictionReady = false;
  self->thermal.temperatureReady = false;
  self->thermal.cycleValid = false;
  self->thermal.cycleSettled = false;
  self->thermal.cycleAmps = 0.0f;
  self->thermal.cycleVolts = 0.0f;
  self->thermal.cyclePwm = 0;
  self->thermal.cycleDrive = 0;
  self->thermal.cycleUs = 0U;
  (void)memset(&self->scan.cycleResult, 0, sizeof(self->scan.cycleResult));
  self->scan.cycleResultStatus = HAL_NONE;
  self->scan.cycleResultSequence = 0U;
  self->scan.lastSequence = 0U;
  self->scan.blocks = 0U;
  self->scan.collectUs = 0U;
  self->scan.gaps = 0U;
  self->scan.frameNs = 0U;
  self->scan.running = false;
  self->thermal.driveResistance = VP37_DRIVE_REFERENCE_OHMS;
  self->thermal.driveObservationOhms = 0.0f;
  self->thermal.driveLearning = false;
  self->thermal.driveLearnedSamples = 0U;
  self->thermal.driveCorrection = 1.0f;
  self->thermal.driveSamples = 0U;
  self->thermal.driveUpdatedMs = 0U;
  self->thermal.driveObservedMs = 0U;
  self->thermal.driveFirstSampleMs = 0U;
  self->thermal.driveLastCycleUs = 0U;
  self->thermal.driveCycleSeen = false;
  self->thermal.driveVoltageReference = 0.0f;
  self->thermal.driveVoltageChangedMs = 0U;
  self->thermal.driveVoltageReady = false;
  self->thermal.driveVoltageSettled = false;
  self->thermal.driveResistanceReady = false;
  self->thermal.driveCompensationEnabled = true;
  self->thermal.driveCompensationUsed = false;
  self->thermal.scale = 1.0f;
  self->thermal.scaleReady = false;
  self->pid.integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  self->pid.integralDeadbandHz = (float)VP37_PID_DEADBAND;
  self->feedforward.motionBoostUp = VP37_PWM_FF_MOTION_BOOST_DEFAULT;
  self->feedforward.motionBoostDown = VP37_PWM_FF_DESCENT_BOOST;
  for (uint32_t i = 0U; i < COUNTOF(self->feedforward.mapTrim); i++) {
    self->feedforward.mapTrim[i] = 0.0f;
  }
  self->feedforward.mapTrimApplied = 0.0f;
  self->feedforward.mapTrimEnabled = false;
  self->pid.integralHoldEntered = false;
  self->feedforward.mapTrimTransfers = 0U;
  // The bench cap equals the bottom of the position profile, so it only ever
  // limits what a console command lowered.
#if ECU_FUNCTIONAL_TESTS_ENABLED
  self->pid.integralOverride = VP37_BENCH_INTEGRAL_CAP_PWM;
#else
  self->pid.integralOverride = 0.0f;
#endif

  if (self->pid.controller == NULL) {
    self->pid.controller = hal_pid_controller_create();
    if (self->pid.controller == NULL) {
      derr("VP37 init failed: cannot create PID controller");
      return VP37_INIT_PID_CREATE_FAILED;
    }
  }

  VP37_setVP37PID(self, VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, false);
  hal_pid_controller_set_tf(self->pid.controller, self->pid.tf);
  hal_pid_controller_set_max_integral(self->pid.controller,
                                      VP37_PID_MAX_INTEGRAL);

  valToPWM(PIO_VP37_ANGLE, 0);

  if (!VP37_makeCalibration(self)) {
    VP37_updateAdjustometerPosition(self);
    VP37_enableVP37(self, false);
    return VP37_INIT_CALIBRATION_FAILED;
  }
  VP37_updateAdjustometerPosition(self);
  self->demand.target = -1;
  self->demand.desired = -1;

  VP37_enableVP37(self, self->feedback.calibrationDone);

  self->vp37Initialized = true;
  return VP37_INIT_OK;
}

void VP37_enableVP37(VP37Pump *self, bool enable) {
  (void)self;
  pcf8574_write(PCF8574_O_VP37_ENABLE, enable);
  deb("vp37 enabled: %d", VP37_isVP37Enabled(self));
}

void VP37_stop(VP37Pump *self) {
  VP37_resetCurrentControl(self);
  self->vp37Initialized = false;
  self->output.finalPWM = 0;
  self->output.lastPWMval = 0;
  valToPWM(PIO_VP37_RPM, 0);
  VP37_enableVP37(self, false);
}

bool VP37_isVP37Enabled(VP37Pump *self) {
  (void)self;
  return pcf8574_read(PCF8574_O_VP37_ENABLE);
}

/**
 * @brief Whether the instance can take a demand at all.
 * @param self Controller instance to check.
 * @return HAL_OK when a demand may be published, HAL_EINVAL for NULL, or
 * HAL_ESTATE while the stroke has not been calibrated.
 */
static hal_status_t VP37_demandAcceptance(const VP37Pump *self) {
  hal_status_t status = HAL_OK;
  if (self == NULL) {
    status = HAL_EINVAL;
  } else if (!self->feedback.calibrationDone) {
    status = HAL_ESTATE;
  } else {
    // Calibrated and ready to take a demand.
  }
  return status;
}

/**
 * @brief Hand one accepted demand to the control loop.
 * @param self Controller instance, already checked and calibrated.
 * @param percent Position in percent of the calibrated stroke.
 * @param target The same position in feedback counts.
 * @note Both entry points end here, so the loop sees one demand whichever
 * unit the caller works in, and the settle timer restarts only when the
 * target really moves.
 */
static void VP37_publishDemand(VP37Pump *self, float percent, int32_t target) {
  self->demand.requestedPercent = percent;
  if (target != self->demand.target) {
    self->demand.targetChangedMs = hal_millis();
  }
  self->demand.target = target;
}

hal_status_t VP37_setPositionDemandPercentage(VP37Pump *self, float percent) {
  hal_status_t status = VP37_demandAcceptance(self);
  if (status == HAL_OK) {
    const bool valid = isfinite(percent);
    const float requested = valid ? hal_constrain(percent, 0.0f, 100.0f) : 0.0f;
    VP37_publishDemand(
        self, requested,
        (int32_t)hal_math_map_f32(requested, 0.0f, 100.0f,
                                  (float)self->feedback.adjustMin,
                                  (float)self->feedback.adjustMax));
    status = valid ? HAL_OK : HAL_EINVAL;
  }
  return status;
}

hal_status_t VP37_setPositionDemandValue(VP37Pump *self, int32_t value) {
  hal_status_t status = VP37_demandAcceptance(self);
  if (status == HAL_OK) {
    const int32_t target = hal_constrain(value, self->feedback.adjustMin,
                                         self->feedback.adjustMax);
    // Percent follows the counts, so telemetry and the rest policy read the
    // same demand whichever entry point set it.
    VP37_publishDemand(
        self,
        hal_math_map_f32((float)target, (float)self->feedback.adjustMin,
                         (float)self->feedback.adjustMax, 0.0f, 100.0f),
        target);
  }
  return status;
}

int32_t VP37_getPositionDemandMinValue(const VP37Pump *self) {
  return self != NULL ? self->feedback.adjustMin : -1;
}

int32_t VP37_getPositionDemandMaxValue(const VP37Pump *self) {
  return self != NULL ? self->feedback.adjustMax : -1;
}

/**
 * @brief Whether zero demand has finished its descent and the drive rests.
 * @return True when the last demand was zero and the slewed target has reached
 * the calibrated bottom; always false when the release is compiled out.
 * @note One definition for the release and for the supply path: at rest no
 * current flows, so the local divider may be trained and the shunt capture
 * has nothing to offer. With VP37_PWM_DISABLE_AT_MIN_POSITION at 0 the loop
 * holds the bottom under drive and neither of those holds.
 */
bool VP37_demandAtRest(const VP37Pump *self) {
#if VP37_PWM_DISABLE_AT_MIN_POSITION
  return (self->demand.requestedPercent <= (float)VP37_PERCENT_MIN) &&
         (self->demand.desiredPosition <= (float)self->feedback.adjustMin);
#else
  (void)self;
  return false;
#endif
}

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
  const uint32_t collectStartedUs = hal_micros();
  (void)VP37_serviceCurrentScan(self);
  self->scan.collectUs = hal_micros() - collectStartedUs;

  if (!VP37_updateAdjustometerPosition(self)) {
    if (hal_elapsed_u32(hal_millis(), self->feedback.commLostSince,
                        VP37_ADJ_COMM_CUTOFF_MS)) {
      VP37_stop(self);
      derr("VP37 disabled: feedback communication timeout");
    }
  } else if (!self->feedback.fresh ||
             (self->feedback.lastStatus &
              (ADJ_STATUS_SIGNAL_LOST | ADJ_STATUS_BASELINE_PENDING)) != 0U) {
    VP37_stop(self);
    derr("VP37 disabled: invalid feedback status:%u fresh:%d",
         self->feedback.lastStatus, self->feedback.fresh);
  } else if ((int32_t)getGlobalValue(F_RPM) > RPM_MAX_EVER) {
    VP37_stop(self);
    derr("VP37 disabled: RPM too high");
  } else {
    self->pidDtUs = self->pidStarted ? nowUs - self->pidLastUs : periodUs;
    self->pidLastUs = nowUs;
    self->pidStarted = true;
    VP37_positionCycle(self);
  }
#if ECU_FUNCTIONAL_TESTS_ENABLED
  VP37_traceRecord(self);
#endif
  self->controlExecUs = hal_micros() - nowUs;
}

/**
 * @brief Execute the inner VP37 quantity-control cycle.
 * @note One step builds the command in this order: slew the demand, track the
 * ramp for the motion feedforward, look the holding command up, set the
 * correction authority, scale for supply and temperature, release the actuator
 * at rest, bound the correction, step the loop, compose and write. The input
 * is a source-independent position demand for the N146/G149-like inner loop.
 */
static void VP37_positionCycle(VP37Pump *self) {
  if (self->demand.target < 0) {
    return;
  }
  VP37Cycle cycle;
  VP37_beginCycle(self, &cycle);
  VP37_rampDemand(self, &cycle);
  VP37_blendMotion(self, &cycle);
  // FF and PID share the warm reference domain; temperature scales the sum.
  self->feedforward.pwm = VP37_feedForward(self, self->demand.desired);
  VP37_updateAuthority(self, &cycle);
  VP37_updateMultipliers(self, &cycle);
  if (VP37_releaseAtRest(self)) {
    return;
  }
  VP37_updateCurrentControl(self, cycle.dt);
  VP37_boundCorrection(self, &cycle);
  if (!VP37_stepCorrection(self, &cycle)) {
    return;
  }
  VP37_composeCommand(self, &cycle);
}

/**
 * @brief Capture what this step needs to know about the previous one.
 * @param self VP37 controller instance to read.
 * @param cycle Step context to fill.
 */
static void VP37_beginCycle(const VP37Pump *self, VP37Cycle *cycle) {
  cycle->previousDesired = self->demand.desired;
  cycle->previousPosition = cycle->previousDesired < 0
                                ? (float)self->demand.target
                                : self->demand.desiredPosition;
  cycle->dt = (float)self->pidDtUs * 0.000001f;
  cycle->stationaryTarget = hal_millis_deadline_expired(
      self->demand.targetChangedMs, VP37_TARGET_STABLE_MS);
  cycle->ki = 0.0f;
  cycle->outputScale = 1.0f;
}

/**
 * @brief Slew the demanded position toward the target.
 * @param self VP37 controller instance to update.
 * @param cycle Step context.
 * @note Rising demand slews slower in the upper stroke, and slower still once
 * the target has settled, so a standing target is approached without
 * overshoot. The error to the measured position follows from the result.
 */
static void VP37_rampDemand(VP37Pump *self, const VP37Cycle *cycle) {
  if (self->demand.desired < 0) {
    self->demand.desiredPosition = (float)self->demand.target;
  } else {
    const float travel =
        (float)self->feedback.adjustMax - (float)self->feedback.adjustMin;
    const float delta =
        (float)self->demand.target - self->demand.desiredPosition;
    const float upperStart =
        (float)self->feedback.adjustMin +
        travel * (VP37_DESIRED_UPPER_SLEW_START_PERCENT * 0.01f);
    float rate = delta > 0.0f && self->demand.desiredPosition >= upperStart
                     ? VP37_DESIRED_UPPER_SLEW_PERCENT_PER_SECOND
                     : VP37_DESIRED_SLEW_PERCENT_PER_SECOND;
    if (delta > 0.0f && cycle->stationaryTarget) {
      rate = self->demand.desiredPosition >= upperStart
                 ? VP37_STATIONARY_UPPER_SLEW_PERCENT_PER_SECOND
                 : VP37_STATIONARY_SLEW_PERCENT_PER_SECOND;
    }
    const float step = travel * (rate * 0.01f) * cycle->dt;
    self->demand.desiredPosition += hal_constrain(delta, -step, step);
  }
  self->demand.desired = (int32_t)self->demand.desiredPosition;
  self->pid.error = self->demand.desired - self->feedback.position;
}

/**
 * @brief Track the ramp's rate for the motion feedforward.
 * @param self VP37 controller instance to update.
 * @param cycle Step context.
 * @note Rise and fall are filtered separately: the upward term scales the
 * map's motion column, the downward term lets the return spring work.
 */
static void VP37_blendMotion(VP37Pump *self, const VP37Cycle *cycle) {
  const float travel =
      (float)self->feedback.adjustMax - (float)self->feedback.adjustMin;
  const float upwardStep =
      travel * (VP37_PWM_FF_MOTION_REFERENCE_RATE * 0.01f) * cycle->dt;
  const float maxRise =
      VP37_DESIRED_SLEW_PERCENT_PER_SECOND / VP37_PWM_FF_MOTION_REFERENCE_RATE;
  const float rise =
      (cycle->stationaryTarget ? VP37_STATIONARY_MOTION_WEIGHT : 1.0f) *
      (upwardStep > 0.0f ? hal_constrain((self->demand.desiredPosition -
                                          cycle->previousPosition) /
                                             upwardStep,
                                         0.0f, maxRise)
                         : 0.0f);
  self->feedforward.riseBlend += (rise - self->feedforward.riseBlend) *
                                 cycle->dt /
                                 (VP37_PWM_FF_MOTION_FILTER_S + cycle->dt);
  const float fall =
      (cycle->stationaryTarget ? VP37_STATIONARY_MOTION_WEIGHT : 1.0f) *
      (upwardStep > 0.0f ? hal_constrain((cycle->previousPosition -
                                          self->demand.desiredPosition) /
                                             upwardStep,
                                         0.0f, maxRise)
                         : 0.0f);
  self->feedforward.fallBlend += (fall - self->feedforward.fallBlend) *
                                 cycle->dt /
                                 (VP37_PWM_FF_MOTION_FILTER_S + cycle->dt);
}

/**
 * @brief Set the correction authority for this step.
 * @param self VP37 controller instance to update.
 * @param cycle Step context; receives the integral gain in force.
 * @note Correction and integral authority stay in the same reference domain
 * as the feedforward. Applying the measured temperature here as well would
 * compensate twice.
 */
static void VP37_updateAuthority(VP37Pump *self, VP37Cycle *cycle) {
  self->pid.positiveLimit = VP37_PID_CORR_LIMIT_POSITIVE_COLD;
  if (self->feedback.commLostSince == 0U) {
    self->pid.positiveLimit = VP37_computePositiveCorrectionLimit(
        VP37_PWM_REFERENCE_TEMP_C, self->feedback.lastStatus,
        self->feedforward.pwm);
  }
  self->pid.integralLimit = VP37_integralLimit(self);
  cycle->ki = hal_pid_controller_get_ki(self->pid.controller);
  const float maxIntegral =
      cycle->ki > 0.0f ? self->pid.integralLimit / cycle->ki : 0.0f;
  hal_pid_controller_set_max_integral(self->pid.controller, maxIntegral);
}

/**
 * @brief Refresh the supply and thermal multipliers of the command.
 * @param self VP37 controller instance to update.
 * @param cycle Step context; receives the product of both multipliers.
 * @note The product scales feedforward and correction alike. Supply latency
 * still matters: the multiplier can only reject changes already measured.
 */
static void VP37_updateMultipliers(VP37Pump *self, VP37Cycle *cycle) {
  self->supply.lastVolts = getGlobalValue(F_VOLTS);
  self->supply.localVolts = getLocalSystemSupplyVoltage();
  if (self->supply.lastVolts < VP37_MIN_COMPENSATION_VOLTAGE) {
    self->supply.lastVolts = VP37_MIN_COMPENSATION_VOLTAGE;
  }
  self->thermal.lastFuelTemp = getGlobalValue(F_FUEL_TEMP);
  VP37_updateVoltageCorrection(self, cycle->dt);
  VP37_updateTemperatureCorrection(self, cycle->dt);
  VP37_updateDriveCorrection(self, cycle->dt);
  VP37_updateThermalScale(self, cycle->dt);
  cycle->outputScale = self->supply.correction * self->thermal.scale;
}

/**
 * @brief Release the spring-return actuator once the commanded descent ends.
 * @return True when the step ends here with the drive off.
 * @note Neither feedforward nor a retained integral may energize the actuator
 * at zero demand, so every term is cleared before the next demand arrives.
 */
static bool VP37_releaseAtRest(VP37Pump *self) {
  self->demand.atRest = VP37_demandAtRest(self);
  if (self->demand.atRest) {
    VP37_resetCurrentControl(self);
    VP37_updateDerivativeGain(self, false);
    hal_pid_controller_reset(self->pid.controller);
    self->pid.terms = (hal_pid_terms_t){0};
    self->feedforward.pwm = 0.0f;
    self->feedforward.riseBlend = 0.0f;
    self->feedforward.fallBlend = 0.0f;
    self->feedforward.motion = 0.0f;
    self->pid.correction = 0.0f;
    self->output.pwmValue = 0.0f;
    self->pid.negativeLimit = 0.0f;
    self->pid.upperLimit = 0.0f;
    self->pid.saturatedHigh = false;
    self->pid.integralHold = false;
    self->pid.integralHoldEnterPending = false;
    self->pid.integralHoldReleasePending = false;
    self->pid.softFloorActive = false;
    self->output.pwmLimited = false;
    VP37_writeQuantityPWM(self, 0);
  }
  return self->demand.atRest;
}

/**
 * @brief Express every actuator limit in the correction domain.
 * @param self VP37 controller instance to update.
 * @param cycle Step context.
 * @note At a settled demand the climb floor follows the learned holding trim.
 * A moving demand retains the acceleration reserve of the original floor.
 */
static void VP37_boundCorrection(VP37Pump *self, const VP37Cycle *cycle) {
  float lowerCommand =
      fmaxf((float)VP37_PWM_MIN / cycle->outputScale,
            self->feedforward.pwm - VP37_PID_CORR_LIMIT_NEGATIVE);
  const float upperCommand = (float)VP37_PWM_MAX / cycle->outputScale;
  self->pid.softFloorActive = false;
  if (self->feedback.position < self->demand.desired) {
    const bool targetSettled = cycle->stationaryTarget &&
                               (self->demand.desired == self->demand.target);
    const float holdingTrim =
        targetSettled ? fminf(self->pid.terms.integral, 0.0f) : 0.0f;
    const float floor = self->feedforward.pwm + holdingTrim -
                        (float)VP37_PWM_FF_SOFT_FLOOR_MARGIN;
    if (floor > lowerCommand) {
      lowerCommand = floor;
      self->pid.softFloorActive = true;
    }
  }
  self->pid.negativeLimit = fmaxf(-VP37_PID_CORR_LIMIT_NEGATIVE,
                                  lowerCommand - self->feedforward.pwm) -
                            self->currentControl.correctionPwm;
  self->pid.upperLimit =
      fminf(self->pid.positiveLimit, upperCommand - self->feedforward.pwm) -
      self->currentControl.correctionPwm;
  hal_pid_controller_set_output_limits(
      self->pid.controller, self->pid.negativeLimit, self->pid.upperLimit);
}

/**
 * @brief Step the correction loop under this step's integration rules.
 * @return False when the loop failed and the pump has been stopped.
 * @note Ramp tracking lag must not build a new holding trim in either
 * direction; an existing trim may unwind, including a reversal before zero
 * release. Supply changes are scaled out of the command before it reaches
 * the actuator, so they never freeze integration.
 */
static bool VP37_stepCorrection(VP37Pump *self, const VP37Cycle *cycle) {
  const bool rampWindup =
      cycle->previousDesired >= 0 &&
      self->demand.desired != cycle->previousDesired &&
      self->pid.terms.integral * cycle->ki * (float)self->pid.error >= 0.0f;
  const bool targetSettled =
      cycle->stationaryTarget && (self->demand.desired == self->demand.target);
  VP37_updateDerivativeGain(self, targetSettled);
  self->pid.integralDeadbandHz = VP37_integralDeadband(self);
  VP37_updateIntegralHold(self, targetSettled);
  const bool freezeIntegral = rampWindup || self->pid.integralHold;
  const float integralDeadband = freezeIntegral ? fabsf((float)self->pid.error)
                                                : self->pid.integralDeadbandHz;
  const hal_status_t pidStatus =
      hal_pid_controller_step_ex(self->pid.controller, (float)self->pid.error,
                                 (float)self->feedback.position, cycle->dt,
                                 integralDeadband, &self->pid.terms);
  bool stepped = true;
  if (pidStatus != HAL_OK) {
    VP37_stop(self);
    derr("VP37 PID step failed: %s", hal_status_to_string(pidStatus));
    stepped = false;
  }
  return stepped;
}

/**
 * @brief Compose the command from feedforward and correction and write it.
 * @param self VP37 controller instance to update.
 * @param cycle Step context.
 * @note The learned trim absorbs a standing integral before the command is
 * scaled to the rail; the clamp to the hardware range is recorded so the
 * telemetry can tell a limited command from a free one.
 */
static void VP37_composeCommand(VP37Pump *self, const VP37Cycle *cycle) {
  self->pid.correction = self->pid.terms.output;
  VP37_transferIntegralToMapTrim(self);
  self->pid.saturatedHigh = self->pid.terms.saturated_high;
  self->output.pwmValue = self->feedforward.pwm + self->pid.correction;
  const float compensatedPWM =
      (self->output.pwmValue + self->currentControl.correctionPwm) *
      cycle->outputScale;
  self->output.finalPWM = (int32_t)compensatedPWM;
  self->output.pwmLimited = (self->output.finalPWM < VP37_PWM_MIN) ||
                            (self->output.finalPWM > VP37_PWM_MAX);
  self->output.finalPWM =
      hal_constrain(self->output.finalPWM, VP37_PWM_MIN, VP37_PWM_MAX);
  self->pid.softFloorActive =
      self->pid.softFloorActive && self->pid.terms.saturated_low;

  VP37_recordCurrentCommand(self, self->output.pwmValue, self->output.finalPWM,
                            hal_micros());
  VP37_writeQuantityPWM(self, self->output.finalPWM);
}

static void VP37_writeQuantityPWM(VP37Pump *self, int32_t pwm) {
  self->output.finalPWM = pwm;
  if (self->output.lastPWMval != pwm) {
    self->output.lastPWMval = pwm;
    valToPWM(PIO_VP37_RPM, pwm);
  }
}
