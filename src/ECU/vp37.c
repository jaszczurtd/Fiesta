#include "vp37.h"
#include "ecu_unit_testing.h"
#include <math.h>
#include <string.h>

#include <hal/timers/hal_soft_timer.h>
#include <utils/multicoreWatchdog.h>

#define VP37_LOCAL_VOLTAGE_VALID_MIN_V 5.0f
// Upper end of the calibrated range. A reading above it still scales the
// command: the divider saturates near 18.8 V, so a saturated reading is a
// lower bound of the rail and can only reduce drive. It never trains the
// scale. The 15 V dual-fault value is for a missing reading, not a high one.
#define VP37_LOCAL_VOLTAGE_VALID_MAX_V 17.0f
#define VP37_LOCAL_VOLTAGE_SCALE_MIN 0.8f
#define VP37_LOCAL_VOLTAGE_SCALE_MAX 1.2f
#define VP37_LOCAL_VOLTAGE_SCALE_FILTER_S 1.0f
#define VP37_LOCAL_VOLTAGE_STABLE_DELTA_V 0.1f

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
 * @return True when the feedback transfer succeeds.
 * @note The refreshed values are a project-local G149-like quantity feedback
 * plus G81-like fuel temperature and supply-voltage telemetry.
 */
static bool VP37_updateAdjustometerPosition(VP37Pump *self);

static VP37TraceSample VP37_controlSample(const VP37Pump *self) {
  const VP37TraceSample sample = {
      .us = self->controlLastUs,
      .dt = self->controlDtUs,
      .sequence = self->controlSequence,
      .throttle = self->lastThrottle,
      .target = self->desiredAdjustometerTarget,
      .desired = self->desiredAdjustometer,
      .measured = self->currentAdjustometerPosition,
      .pwm = self->finalPWM,
      .ff = self->pwmFeedForward,
      .low = self->pidNegativeLimit,
      .motionFF = self->feedForwardMotion,
      .high = self->pidUpperLimit,
      .volts = self->lastVolts,
      .localVolts = self->localVolts,
      .compensationInputVolts = self->compensationInputVolts,
      .compensationVolts = self->compensationVolts,
      .voltageCorrection = self->voltageCorrection,
      .cycleVoltageUsed = self->cycleVoltageUsed,
      .voltageOverRange = self->voltageOverRange,
      .mapTrim = self->mapTrimApplied,
      .thermalScale = self->thermalScale,
      .integralHold = self->integralHold,
      .fuelTemp = self->lastFuelTemp,
      .temperatureCorrection = self->temperatureCorrection,
      .terms = self->pidTerms,
      .softFloor = self->softFloorActive,
      .hardwareClamp = self->pwmLimited,
      .quantityAtRest = self->quantityAtRest,
      .status = self->lastAdjustometerStatus,
      .rawHz = self->feedbackRawHz,
      .filteredHz = self->feedbackFilteredHz,
      .sampleNumber = self->feedbackNumber,
      .measuredUs = self->feedbackUs,
      .ageUs = self->feedbackAgeUs,
      .readStatus = self->feedbackReadStatus,
      .readUs = self->feedbackReadUs,
      .retries = self->feedbackRetries,
      .fresh = self->feedbackFresh,
      .cyclicDelayMs = testsCyclicDelayMs(),
      .pidDtUs = self->pidDtUs};
  return sample;
}

#if ECU_FUNCTIONAL_TESTS_ENABLED
static struct {
  VP37TraceSample samples[VP37_TRACE_SAMPLES];
  uint32_t count, next;
  bool recording;
} s_trace;

hal_status_t VP37_startTrace(VP37Pump *self) {
  if (self == NULL) {
    return HAL_EINVAL;
  }
  if (s_trace.recording || (s_trace.count != 0U)) {
    return HAL_EBUSY;
  }
  if (!self->vp37Initialized) {
    return HAL_EAGAIN;
  }
  s_trace.next = 0U;
  s_trace.recording = true;
  return HAL_OK;
}

bool VP37_traceCapturing(void) { return s_trace.recording; }

hal_status_t VP37_readTrace(VP37Pump *self, VP37TraceSample *sample) {
  if ((self == NULL) || (sample == NULL)) {
    return HAL_EINVAL;
  }
  if (!self->vp37Initialized) {
    s_trace.recording = false;
  }
  if (s_trace.recording) {
    return HAL_EAGAIN;
  }
  if (s_trace.next >= s_trace.count) {
    return HAL_ENOENT;
  }
  *sample = s_trace.samples[s_trace.next++];
  if (s_trace.next == s_trace.count) {
    s_trace.count = 0U;
    s_trace.next = 0U;
  }
  return HAL_OK;
}
#endif

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

static void VP37_updateTemperatureCorrection(VP37Pump *self, float dt) {
  const uint8_t invalid = ADJ_STATUS_SIGNAL_LOST | ADJ_STATUS_FUEL_TEMP_BROKEN |
                          ADJ_STATUS_BASELINE_PENDING;
  if (!isfinite(self->lastFuelTemp) || self->lastFuelTemp < 0.0f ||
      self->lastFuelTemp > VP37_THERMAL_TEMP_VALID_MAX_C ||
      (self->lastAdjustometerStatus & invalid) != 0U) {
    return; // Keep the last valid factor; initialization uses unity.
  }
  const float reference =
      1.0f + VP37_COPPER_TEMP_COEFFICIENT *
                 (VP37_PWM_REFERENCE_TEMP_C - VP37_THERMAL_REFERENCE_TEMP_C);
  const float resistance =
      1.0f + VP37_COPPER_TEMP_COEFFICIENT *
                 (self->lastFuelTemp - VP37_THERMAL_REFERENCE_TEMP_C);
  const float factor =
      hal_constrain(resistance / reference, VP37_TEMPERATURE_FACTOR_MIN,
                    VP37_TEMPERATURE_FACTOR_MAX);
  const float target =
      1.0f + self->temperatureCompensationWeight * (factor - 1.0f);
  if (!self->temperatureReady) {
    self->temperatureCorrection = target;
    self->temperatureReady = true;
  } else {
    self->temperatureCorrection += (target - self->temperatureCorrection) * dt /
                                   (VP37_TEMPERATURE_FILTER_S + dt);
  }
}

/**
 * @brief Track the drive-path resistance the shunt capture actually sees.
 * @param self VP37 controller instance to update.
 * @param dt Control period in seconds.
 * @return None.
 * @note Coil self-heating moves the required command by several percent while
 * the fuel temperature barely changes, so the measured ratio replaces the
 * fuel-temperature model. A rejected or stale capture keeps the last value and
 * never contributes zero ohms.
 */
static void VP37_updateDriveCorrection(VP37Pump *self, float dt) {
  // A ready estimate keeps scaling the command until it goes stale; motion
  // rejects most captures, and flipping back to the model on every rejected
  // cycle stepped the command by the whole thermal difference.
  self->driveCompensationUsed =
      self->driveCompensationEnabled && self->driveResistanceReady &&
      !hal_millis_deadline_expired(self->driveUpdatedMs, VP37_DRIVE_STALE_MS);
  if (!self->driveCompensationEnabled || !self->cycleCurrentValid ||
      !isfinite(self->cycleCurrentAmps) || !isfinite(self->cycleCurrentVolts)) {
    return;
  }
  if (hal_elapsed_u32(hal_micros(), self->cycleCurrentUs,
                      VP37_DRIVE_MAX_AGE_US)) {
    return;
  }
  const int32_t drive = self->cycleCurrentDrive;
  if ((drive < VP37_DRIVE_MIN_PWM) || (drive > VP37_PWM_MAX) ||
      (self->cycleCurrentAmps < VP37_DRIVE_MIN_CURRENT_A) ||
      (self->cycleCurrentVolts < VP37_LOCAL_VOLTAGE_VALID_MIN_V) ||
      (self->cycleCurrentVolts > VP37_LOCAL_VOLTAGE_VALID_MAX_V)) {
    return;
  }
  // Reject a capture that belongs to a different command than the live one.
  const int32_t commandDelta = drive - self->finalPWM;
  const int32_t gateDelta = self->cycleCurrentPwm - drive;
  if ((commandDelta > VP37_DRIVE_COMMAND_MATCH_COUNTS) ||
      (commandDelta < -VP37_DRIVE_COMMAND_MATCH_COUNTS) ||
      (gateDelta > VP37_DRIVE_PWM_MATCH_COUNTS) ||
      (gateDelta < -VP37_DRIVE_PWM_MATCH_COUNTS)) {
    return;
  }

  const float duty = (float)drive / (float)PWM_RESOLUTION;
  const float resistance =
      (duty * self->cycleCurrentVolts) / self->cycleCurrentAmps;
  if (!isfinite(resistance) || (resistance <= 0.0f)) {
    return;
  }
  // Always filter from the reference. Seeding from the first accepted capture
  // used to adopt it whole, and a capture taken while the actuator is slamming
  // through its stroke reconstructs a resistance that is not the coil's.
  if (!(self->driveResistance > 0.0f)) {
    self->driveResistance = VP37_DRIVE_REFERENCE_OHMS;
  }
  self->driveResistance +=
      (resistance - self->driveResistance) * dt / (VP37_DRIVE_FILTER_S + dt);
  if (self->driveSamples == 0U) {
    self->driveFirstSampleMs = hal_millis();
  }
  if (self->driveSamples < VP37_DRIVE_READY_SAMPLES) {
    self->driveSamples++;
  }
  self->driveUpdatedMs = hal_millis();
  self->driveResistanceReady =
      (self->driveSamples >= VP37_DRIVE_READY_SAMPLES) &&
      hal_millis_deadline_expired(self->driveFirstSampleMs,
                                  VP37_DRIVE_SETTLE_MS);
  self->driveCorrection =
      hal_constrain(self->driveResistance / VP37_DRIVE_REFERENCE_OHMS,
                    VP37_TEMPERATURE_FACTOR_MIN, VP37_TEMPERATURE_FACTOR_MAX);
  self->driveCompensationUsed =
      self->driveCompensationEnabled && self->driveResistanceReady;
}

/**
 * @brief Slew the thermal multiplier toward whichever source is in force.
 * @param self VP37 controller instance to update.
 * @param dt Control period in seconds.
 * @return None.
 * @note One multiplier only: the measured resistance already contains the fluid
 * effect the fuel-temperature model estimates, so they never stack. They do
 * disagree by whatever the coil has self-heated, so handing over between them
 * used to step the command. Resistance moves on a thermal time scale, far below
 * the rate limit, so the ramp only blunts a handover and never lags a real
 * change. Only the very first value is taken whole: a cyclic ramp touches zero
 * demand several times a second, so snapping at rest would hand the command
 * every wild estimate the ramp produces and defeat the limit where it matters
 * most.
 */
static void VP37_updateThermalScale(VP37Pump *self, float dt) {
  const float target = self->driveCompensationUsed
                           ? self->driveCorrection
                           : self->temperatureCorrection;
  if (!self->thermalScaleReady) {
    self->thermalScale = target;
    self->thermalScaleReady = true;
  } else {
    const float step = VP37_THERMAL_SCALE_SLEW_PER_S * dt;
    const float delta = target - self->thermalScale;
    if (delta > step) {
      self->thermalScale += step;
    } else if (delta < -step) {
      self->thermalScale -= step;
    } else {
      self->thermalScale = target;
    }
  }
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

static bool VP37_updateAdjustometerPosition(VP37Pump *self) {
  adjustometer_reading_t reading;
  getVP37Adjustometer(&reading);
  self->feedbackReadStatus = reading.fastFeedback
                                 ? reading.readStatus
                                 : (reading.commOk ? HAL_OK : HAL_EBUS);
  self->feedbackReadUs = reading.readUs;
  self->feedbackRetries = reading.readRetries;
  if (reading.commOk) {
    self->currentAdjustometerPosition = reading.pulseHz;
    self->adjCommLostSince = 0;
    self->adjCommFailed = false;
    self->lastAdjustometerStatus = reading.status;
    self->feedbackFresh = !reading.fastFeedback || reading.feedbackFresh;
    self->feedbackRawHz = reading.rawHz;
    self->feedbackFilteredHz = reading.signalHz;
    self->feedbackNumber = reading.sampleNumber;
    self->feedbackUs = reading.measuredUs;
    self->feedbackAgeUs = reading.ageUs;

    setGlobalValue(F_FUEL_TEMP, reading.fuelTempC);
    setGlobalValue(F_VOLTS, reading.voltageRaw * 0.1f);
  } else {
    self->feedbackFresh = false;
    if (!self->adjCommFailed) {
      self->adjCommFailed = true;
      self->adjCommLostSince = hal_millis();
    }
  }
  return reading.commOk;
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
    if (!reading.commOk || (reading.fastFeedback && !reading.feedbackFresh) ||
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

static void VP37_writeQuantityPWM(VP37Pump *self, int32_t pwm) {
  self->finalPWM = pwm;
  if (self->lastPWMval != pwm) {
    self->lastPWMval = pwm;
    valToPWM(PIO_VP37_RPM, pwm);
  }
}

static float VP37_getCompensationInputVoltage(VP37Pump *self, float dt) {
  const bool quantityAtRest =
      (self->lastThrottle <= (float)VP37_PERCENT_MIN) &&
      (self->desiredPosition <= (float)self->VP37_ADJUST_MIN);
  self->cycleVoltageUsed =
      self->cycleVoltageEnabled && self->currentObservationEnabled &&
      !quantityAtRest && self->cycleSupplyValid &&
      isfinite(self->cycleSupplyVolts) &&
      (self->cycleSupplyVolts >= VP37_LOCAL_VOLTAGE_VALID_MIN_V) &&
      !hal_elapsed_u32(hal_micros(), self->cycleSupplyUs,
                       VP37_CYCLE_VOLTAGE_MAX_AGE_US);
  const float localVolts =
      self->cycleVoltageUsed ? self->cycleSupplyVolts : self->localVolts;
  const bool localMeasured =
      isfinite(localVolts) && (localVolts >= VP37_LOCAL_VOLTAGE_VALID_MIN_V);
  const bool localValid =
      localMeasured && (localVolts <= VP37_LOCAL_VOLTAGE_VALID_MAX_V);
  self->voltageOverRange = localMeasured && !localValid;
  const bool adjustometerVoltageValid =
      (self->lastAdjustometerStatus & ADJ_STATUS_VOLTAGE_BAD) == 0U;
  float resultVolts;

  if (!localMeasured) {
    self->localVoltageReady = false;
    resultVolts = adjustometerVoltageValid ? self->lastVolts
                                           : VP37_MAX_EXPECTED_SUPPLY_VOLTAGE;
  } else if (self->voltageOverRange) {
    // Above the calibrated range: scale with the last learned factor and
    // leave the scale alone; the Adjustometer flags its own reading bad here.
    resultVolts = localVolts * self->localVoltageScale;
  } else {
    const float scaleTarget = hal_constrain(self->lastVolts / localVolts,
                                            VP37_LOCAL_VOLTAGE_SCALE_MIN,
                                            VP37_LOCAL_VOLTAGE_SCALE_MAX);
    if (!self->localVoltageReady) {
      if (adjustometerVoltageValid) {
        self->localVoltageScale = scaleTarget;
      }
      self->localVoltageReady = true;
    } else {
      const float localDelta = fabsf(localVolts - self->previousLocalVolts);
      if (quantityAtRest && adjustometerVoltageValid &&
          (localDelta <= VP37_LOCAL_VOLTAGE_STABLE_DELTA_V)) {
        self->localVoltageScale += (scaleTarget - self->localVoltageScale) *
                                   dt /
                                   (VP37_LOCAL_VOLTAGE_SCALE_FILTER_S + dt);
      }
    }
    self->previousLocalVolts = localVolts;
    resultVolts = localVolts * self->localVoltageScale;
  }
  return resultVolts;
}

static void VP37_updateVoltageCorrection(VP37Pump *self, float dt) {
  float measuredVolts = VP37_getCompensationInputVoltage(self, dt);
  if (measuredVolts < VP37_MIN_COMPENSATION_VOLTAGE) {
    measuredVolts = VP37_MIN_COMPENSATION_VOLTAGE;
  }
  self->compensationInputVolts = measuredVolts;
  if (!self->voltageReady) {
    self->compensationVolts = measuredVolts;
    self->voltageReady = true;
  } else if (self->voltageFrozen) {
    // Diagnostic: keep the scale where it is so the supply loop stays open.
  } else if (self->cycleVoltageUsed) {
    // The full-period mean is already free of the intra-period alias, so the
    // scale takes it as is: a manual 15-20 V/s sweep left 0.75-1.3 V behind
    // the 50 ms filter, and cranking edges are faster still.
    self->compensationVolts = measuredVolts;
  } else {
    // The local fallback is a 40 us snapshot; only that path needs smoothing.
    self->compensationVolts += (measuredVolts - self->compensationVolts) * dt /
                               (VP37_VOLTAGE_FILTER_S + dt);
  }
  self->voltageCorrection = NOMINAL_VOLTAGE / self->compensationVolts;
}

/**
 * @brief Locate the two learned-trim knots around a stroke percentage.
 * @param percent Stroke position, clamped to 0..100.
 * @param lower Non-NULL; receives the lower knot index.
 * @param weight Non-NULL; receives the upper knot's share, 0..1.
 * @return None. The last knot pairs with itself.
 */
static void VP37_mapTrimKnots(float percent, uint32_t *lower, float *weight) {
  const float step = 100.0f / (float)(VP37_MAP_TRIM_KNOTS - 1U);
  const float scaled = hal_constrain(percent, 0.0f, 100.0f) / step;
  uint32_t index = (uint32_t)scaled;
  if (index >= (VP37_MAP_TRIM_KNOTS - 1U)) {
    index = VP37_MAP_TRIM_KNOTS - 1U;
    *weight = 0.0f;
  } else {
    *weight = scaled - (float)index;
  }
  *lower = index;
}

/** @brief Learned holding-map residual, interpolated between knots. */
static float VP37_mapTrimAt(const VP37Pump *self, float percent) {
  if (!self->mapTrimEnabled) {
    return 0.0f;
  }
  uint32_t lower = 0U;
  float weight = 0.0f;
  VP37_mapTrimKnots(percent, &lower, &weight);
  const uint32_t upper =
      (lower + 1U < VP37_MAP_TRIM_KNOTS) ? (lower + 1U) : lower;
  return (self->mapTrim[lower] * (1.0f - weight)) +
         (self->mapTrim[upper] * weight);
}

/**
 * @brief Move the settled integral into the learned map trim.
 * @param self VP37 controller instance to update.
 * @return None.
 * @note Runs once when the settled-position hold engages. The trim takes the
 * whole integral and the controller restarts from zero, so this step's output
 * and the next step's feedforward add up to the same command: no bump. A trim
 * that would leave the bound keeps the integral where it is.
 */
static void VP37_transferIntegralToMapTrim(VP37Pump *self) {
  if (!self->mapTrimEnabled || !self->integralHoldEntered) {
    return;
  }
  self->integralHoldEntered = false;
  const float percent = hal_constrain(
      hal_math_map_f32(self->desiredPosition, (float)self->VP37_ADJUST_MIN,
                       (float)self->VP37_ADJUST_MAX, 0.0f, 100.0f),
      0.0f, 100.0f);
  uint32_t lower = 0U;
  float weight = 0.0f;
  VP37_mapTrimKnots(percent, &lower, &weight);
  const uint32_t upper =
      (lower + 1U < VP37_MAP_TRIM_KNOTS) ? (lower + 1U) : lower;
  const float integral = self->pidTerms.integral;
  // Both knots take the whole integral: the interpolated value then rises by
  // exactly that amount at this position, and neighbouring holds average
  // out the friction share of what each of them learned.
  const float lowerCandidate = self->mapTrim[lower] + integral;
  const float upperCandidate = self->mapTrim[upper] + integral;
  if (!isfinite(lowerCandidate) || !isfinite(upperCandidate) ||
      (fabsf(lowerCandidate) > VP37_MAP_TRIM_LIMIT_PWM) ||
      (fabsf(upperCandidate) > VP37_MAP_TRIM_LIMIT_PWM) ||
      (fabsf(integral) < 0.5f)) {
    return;
  }
  self->mapTrim[lower] = lowerCandidate;
  self->mapTrim[upper] = upperCandidate;
  hal_pid_controller_reset(self->adjustController);
  self->pidTerms.integral = 0.0f;
  self->mapTrimTransfers++;
}

/**
 * @brief Demand along the calibrated stroke, the axis every stroke map uses.
 * @param self VP37 controller instance to inspect.
 * @param position Adjustometer position.
 * @return Demand in percent, clamped to the calibrated range.
 */
static float VP37_strokePercent(const VP37Pump *self, float position) {
  return hal_constrain(hal_math_map_f32(position, (float)self->VP37_ADJUST_MIN,
                                        (float)self->VP37_ADJUST_MAX, 0.0f,
                                        100.0f),
                       0.0f, 100.0f);
}

static float VP37_feedForward(VP37Pump *self, int32_t position) {
  // The holding map and its motion column are data in engineMaps.c; this is
  // only the interpolation between its knots and the blending of motion.
  const float percent = VP37_strokePercent(self, (float)position);
  for (size_t i = 1U; i < VP37_FF_KNOTS; ++i) {
    const float *lower = VP37_FF_MAP[i - 1U];
    const float *upper = VP37_FF_MAP[i];
    if (percent <= upper[VP37_FF_COL_PERCENT]) {
      const float holding = hal_math_map_f32(
          percent, lower[VP37_FF_COL_PERCENT], upper[VP37_FF_COL_PERCENT],
          lower[VP37_FF_COL_PWM], upper[VP37_FF_COL_PWM]);
      self->feedForwardMotion =
          (self->feedForwardRiseBlend *
           hal_math_map_f32(
               percent, lower[VP37_FF_COL_PERCENT], upper[VP37_FF_COL_PERCENT],
               lower[VP37_FF_COL_MOTION], upper[VP37_FF_COL_MOTION]) *
           (self->motionBoostUp / VP37_PWM_FF_MOTION_BOOST)) -
          (self->feedForwardFallBlend * self->motionBoostDown);
      self->mapTrimApplied = VP37_mapTrimAt(self, percent);
      return (holding * VP37_PWM_FF_HARDWARE_GAIN) + self->mapTrimApplied +
             self->feedForwardMotion;
    }
  }
  self->feedForwardMotion = 0.0f;
  self->mapTrimApplied = VP37_mapTrimAt(self, 100.0f);
  return (VP37_PWM_FF_AT_MAX * VP37_PWM_FF_HARDWARE_GAIN) +
         self->mapTrimApplied;
}

/**
 * @brief Pick the integration dead zone for the demanded position.
 * @param self VP37 controller instance to inspect.
 * @return Dead zone in hertz, never below the base value.
 * @note The upper stroke settles hundreds of hertz apart for the same command,
 * so integrating small errors there only winds force against the mechanism.
 * Below the taper start the loop keeps its full accuracy.
 */
/**
 * @brief Value of a stroke taper at a demand, both ends held flat.
 * @param knots Rows of {demand [%], value} in ascending demand, laid out
 * row-major as in engineMaps.h.
 * @param count Rows in the table, at least one.
 * @param percent Demand along the stroke.
 * @return The interpolated value, or the first or last one beyond the ends.
 * @note A flat segment returns its value exactly, so a table that starts
 * flat gives its base bit for bit below the taper.
 */
TESTABLE_STATIC float VP37_strokeTaper(const float *knots, size_t count,
                                       float percent) {
  const size_t last = (count - 1U) * VP37_STROKE_TAPER_COLUMNS;
  float value = knots[VP37_TAPER_COL_VALUE];
  if (percent >= knots[last + VP37_TAPER_COL_PERCENT]) {
    value = knots[last + VP37_TAPER_COL_VALUE];
  } else if (percent > knots[VP37_TAPER_COL_PERCENT]) {
    size_t upper = VP37_STROKE_TAPER_COLUMNS;
    while ((upper < last) &&
           (percent > knots[upper + VP37_TAPER_COL_PERCENT])) {
      upper += VP37_STROKE_TAPER_COLUMNS;
    }
    const size_t lower = upper - VP37_STROKE_TAPER_COLUMNS;
    value = hal_math_map_f32(percent, knots[lower + VP37_TAPER_COL_PERCENT],
                             knots[upper + VP37_TAPER_COL_PERCENT],
                             knots[lower + VP37_TAPER_COL_VALUE],
                             knots[upper + VP37_TAPER_COL_VALUE]);
  } else {
    // At or below the first knot: its value.
  }
  return value;
}

static float VP37_integralDeadband(const VP37Pump *self) {
  const float base = VP37_INTEGRAL_DEADBAND_MAP[0U][VP37_TAPER_COL_VALUE];
  float deadband = base;
  if (self->integralDeadbandTopHz > base) {
    // The table holds the default top; the bench may have moved it.
    float taper[VP37_STROKE_TAPER_KNOTS * VP37_STROKE_TAPER_COLUMNS];
    (void)memcpy(taper, VP37_INTEGRAL_DEADBAND_MAP, sizeof(taper));
    taper[((VP37_STROKE_TAPER_KNOTS - 1U) * VP37_STROKE_TAPER_COLUMNS) +
          VP37_TAPER_COL_VALUE] = self->integralDeadbandTopHz;
    deadband =
        VP37_strokeTaper(taper, VP37_STROKE_TAPER_KNOTS,
                         VP37_strokePercent(self, self->desiredPosition));
  }
  return deadband;
}

/** @brief Update the settled-target hysteresis that freezes integration. */
static void VP37_updateIntegralHold(VP37Pump *self, bool targetSettled) {
  const float absoluteError = fabsf((float)self->pidErr);
  // Fixed bands on purpose: widening them with the dead zone froze the
  // integral up to twice the dead zone from the target and left standing
  // errors of 120 Hz on the upper stroke. There the dead zone alone bounds
  // the error; the hold matters where the band is narrower than the zone.
  const float enterHz = (float)VP37_INTEGRAL_HOLD_ENTER_HZ;
  const float exitHz = (float)VP37_INTEGRAL_HOLD_EXIT_HZ;
  self->integralHoldEntered = false;
  if (!targetSettled) {
    self->integralHold = false;
    self->integralHoldEnterPending = false;
    self->integralHoldReleasePending = false;
  } else if (!self->integralHold) {
    if (absoluteError > enterHz) {
      self->integralHoldEnterPending = false;
    } else {
      if (!self->integralHoldEnterPending) {
        self->integralHoldEnterStartedMs = hal_millis();
        self->integralHoldEnterPending = true;
      }
      if (hal_millis_deadline_expired(self->integralHoldEnterStartedMs,
                                      self->integralHoldConfirmMs)) {
        self->integralHold = true;
        self->integralHoldEntered = true;
        self->integralHoldEnterPending = false;
        self->integralHoldReleasePending = false;
      }
    }
  } else if (absoluteError <= exitHz) {
    self->integralHoldReleasePending = false;
  } else if (!self->integralHoldReleasePending) {
    self->integralHoldReleasePending = true;
    self->integralHoldReleaseStartedMs = hal_millis();
  } else {
    if (hal_millis_deadline_expired(self->integralHoldReleaseStartedMs,
                                    VP37_INTEGRAL_HOLD_RELEASE_MS)) {
      self->integralHold = false;
      self->integralHoldEnterPending = false;
      self->integralHoldReleasePending = false;
    }
  }
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

  const float trimLimit = VP37_strokeTaper(
      &VP37_INTEGRAL_LIMIT_MAP[0U][0U], VP37_STROKE_TAPER_KNOTS,
      VP37_strokePercent(self, self->desiredPosition));
  self->pidIntegralLimit = trimLimit;
  if (self->pidIntegralOverride > 0.0f) {
    self->pidIntegralLimit =
        fminf(self->pidIntegralLimit, self->pidIntegralOverride);
  }
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

void VP37_setVP37PID(VP37Pump *self, float kp, float ki, float kd,
                     bool shouldTriggerReset) {
  self->pidKp = kp;
  self->pidKi = ki;
  self->pidKd = kd;
  hal_pid_controller_set_kp(self->adjustController, kp);
  hal_pid_controller_set_ki(self->adjustController, ki);
  hal_pid_controller_set_kd(self->adjustController, kd);

  if (shouldTriggerReset) {
    hal_pid_controller_reset(self->adjustController);
    self->integralHold = false;
    self->integralHoldEnterPending = false;
    self->integralHoldReleasePending = false;
    self->integralHoldReleaseStartedMs = 0U;
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

hal_status_t VP37_serviceCurrentScan(VP37Pump *self) {
  self->scanRunning = hal_adc_scan_is_running();
  self->scanFrameNs = VP37_currentScanFrameNs();
  if (!self->scanRunning) {
    return HAL_ESTATE;
  }
  VP37CurrentPulseResult result;
  uint32_t sequence = 0U;
  const hal_status_t status = VP37_currentScanCollect(&result, &sequence);
  if (sequence == 0U) {
    return status;
  }
  if ((self->scanLastSequence != 0U) &&
      (sequence > (self->scanLastSequence + 1U))) {
    self->scanGaps += sequence - self->scanLastSequence - 1U;
  }
  self->scanLastSequence = sequence;
  self->scanBlocks++;
  self->cycleResult = result;
  self->cycleResultStatus = status;
  self->cycleResultSequence++;
  if (!self->currentObservationEnabled || self->quantityAtRest ||
      (self->finalPWM <= 0)) {
    return status;
  }
  const bool currentUsable = (status == HAL_OK) && result.waveformValid;
  self->cycleSupplyVolts = result.supplyVolts;
  self->cycleSupplyUs = result.cycleStartUs + result.periodUs;
  self->cycleSupplyValid = result.supplyValid;
  // The drive command below belongs to the observation, not to its later use.
  self->cycleCurrentValid = currentUsable;
  self->cycleCurrentAmps = currentUsable ? result.meanAmps : 0.0f;
  self->cycleCurrentVolts = result.supplyValid ? result.supplyVolts : 0.0f;
  self->cycleCurrentPwm = result.pwmCommand;
  self->cycleCurrentDrive = self->finalPWM;
  self->cycleCurrentUs = result.cycleStartUs + result.periodUs;
  return status;
}

void VP37_showCurrentPulse(const VP37Pump *self) {
  const VP37CurrentPulseResult *result = &self->cycleResult;
  deb("VP37 IPULSE us:%lu seq:%lu state_us:%lu pwm:%ld adj:%ld des:%ld "
      "V:%.3f FT:%.1f Ion:%.4f I95:%.4f Ipk:%.4f per:%lu on:%lu "
      "duty:%ld n:%lu gn:%lu clip:%lu zero:%u zv:%u valid:%u status:%d "
      "Vavg:%.4f Vok:%u blk:%lu gaps:%lu gl:%lu",
      (unsigned long)result->cycleStartUs, (unsigned long)self->controlSequence,
      (unsigned long)self->controlLastUs, (long)self->finalPWM,
      (long)self->currentAdjustometerPosition, (long)self->desiredAdjustometer,
      self->compensationVolts, self->lastFuelTemp, result->meanAmps,
      result->p95Amps, result->peakAmps, (unsigned long)result->periodUs,
      (unsigned long)result->onTimeUs, (long)result->pwmCommand,
      (unsigned long)result->samples, (unsigned long)result->guardedSamples,
      (unsigned long)result->clippedSamples, (unsigned)result->zeroRaw,
      result->zeroValid ? 1U : 0U, result->waveformValid ? 1U : 0U,
      (int)self->cycleResultStatus, result->supplyVolts,
      result->supplyValid ? 1U : 0U, (unsigned long)self->scanBlocks,
      (unsigned long)self->scanGaps, (unsigned long)result->glitches);
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
  if (s_trace.recording) {
    s_trace.samples[s_trace.count++] = VP37_controlSample(self);
    if (s_trace.count == COUNTOF(s_trace.samples) || !self->vp37Initialized) {
      s_trace.recording = false;
    }
  }
#endif
}

static void VP37_showControlSample(const VP37TraceSample *sample,
                                   const char *kind) {
  deb("VP37 %s us:%lu dt:%lu n:%lu thr:%.1f tar:%d des:%d adj:%d pwm:%d err:%d "
      "ff:%.1f P:%.1f I:%.1f D:%.1f raw:%.1f corr:%.1f lo:%.1f hi:%.1f "
      "sh:%d sl:%d sf:%d hw:%d rest:%d st:%u hzraw:%lu hz:%lu sn:%lu su:%lu "
      "age:%u io:%d ious:%lu retry:%u fresh:%d pdt:%lu V:%.1f Vl:%.2f "
      "Ve:%.2f Vc:%.3f vcor:%.4f ih:%d vp:%d vhi:%d "
      "ft:%.0f tcf:%.4f tsc:%.4f mff:%.1f cyms:%lu mtrim:%.1f",
      kind, (unsigned long)sample->us, (unsigned long)sample->dt,
      (unsigned long)sample->sequence, sample->throttle, sample->target,
      sample->desired, sample->measured, sample->pwm,
      sample->desired - sample->measured, sample->ff,
      sample->terms.proportional, sample->terms.integral,
      sample->terms.derivative, sample->terms.unconstrained,
      sample->terms.output, sample->low, sample->high,
      sample->terms.saturated_high, sample->terms.saturated_low,
      sample->softFloor, sample->hardwareClamp, sample->quantityAtRest,
      (unsigned int)sample->status, (unsigned long)sample->rawHz,
      (unsigned long)sample->filteredHz, (unsigned long)sample->sampleNumber,
      (unsigned long)sample->measuredUs, (unsigned int)sample->ageUs,
      (int)sample->readStatus, (unsigned long)sample->readUs,
      (unsigned int)sample->retries, sample->fresh,
      (unsigned long)sample->pidDtUs, sample->volts, sample->localVolts,
      sample->compensationInputVolts, sample->compensationVolts,
      sample->voltageCorrection, sample->integralHold, sample->cycleVoltageUsed,
      sample->voltageOverRange, sample->fuelTemp, sample->temperatureCorrection,
      sample->thermalScale, sample->motionFF,
      (unsigned long)sample->cyclicDelayMs, sample->mapTrim);
}

#if ECU_FUNCTIONAL_TESTS_ENABLED
void VP37_showTrace(const VP37TraceSample *sample) {
  VP37_showControlSample(sample, "T");
}
#endif

void VP37_showDebug(VP37Pump *self) {
  // Caller passes a snapshot; serial formatting and I2C run outside the control
  // lock.
  const VP37TraceSample sample = VP37_controlSample(self);
  VP37_showControlSample(&sample, "C");

  static uint32_t lastTelemetryMs = 0U;
  if (hal_millis_interval_elapsed_now(&lastTelemetryMs,
                                      VP37_TELEMETRY_UPDATE)) {
    // A running test borrows the demand; otherwise the configured source owns
    // it. Both are reported under the same name so one log reads the same way.
    const char *const activeTest = testsActiveName();
    const char *const mode =
        activeTest != NULL
            ? activeTest
            : (VP37_ENGINE_OPERATION_MODE != 0 ? "engine" : "potentiometer");
    const uint32_t cycleDelayMs = testsCyclicDelayMs();
    deb("VP37 CFG rev:75 kp:%.4f ki:%.4f kd:%.5f tf:%.4f tu:%.1f "
        "min:%d max:%d V:%.1f Vl:%.2f Ve:%.2f Vc:%.3f vg:%.4f vf:%.3f "
        "vcor:%.4f t:%.1fC imax:%.1f tw:%.2f "
        "tcf:%.4f mode:%s cyclic_ms:%lu slew:%.1f upper_slew:%.1f "
        "ien:%u iconfirm:%lu vsync:%u vuse:%u vfrz:%u Vavg:%.3f pwm_hz:%u "
        "Imeas:%.4f Rdrv:%.4f rcf:%.4f ren:%u ruse:%u rn:%lu "
        "dbtop:%.0f db:%.0f mten:%u mtn:%lu mt50:%.1f mt90:%.1f mt100:%.1f "
        "mup:%.0f mdn:%.0f scan:%u fr:%lu blk:%lu gaps:%lu",
        self->pidKp, self->pidKi, self->pidKd, self->pidTf, self->pidTimeUpdate,
        self->VP37_ADJUST_MIN, self->VP37_ADJUST_MAX, self->lastVolts,
        self->localVolts, self->compensationInputVolts, self->compensationVolts,
        self->localVoltageScale, VP37_VOLTAGE_FILTER_S, self->voltageCorrection,
        self->lastFuelTemp, self->pidIntegralLimit,
        self->temperatureCompensationWeight, self->temperatureCorrection, mode,
        (unsigned long)cycleDelayMs, VP37_DESIRED_SLEW_PERCENT_PER_SECOND,
        VP37_DESIRED_UPPER_SLEW_PERCENT_PER_SECOND,
        self->currentObservationEnabled ? 1U : 0U,
        (unsigned long)self->integralHoldConfirmMs,
        self->cycleVoltageEnabled ? 1U : 0U, self->cycleVoltageUsed ? 1U : 0U,
        self->voltageFrozen ? 1U : 0U, self->cycleSupplyVolts,
        (unsigned)VP37_PWM_FREQUENCY_HZ, self->cycleCurrentAmps,
        self->driveResistance, self->driveCorrection,
        self->driveCompensationEnabled ? 1U : 0U,
        self->driveCompensationUsed ? 1U : 0U,
        (unsigned long)self->driveSamples, self->integralDeadbandTopHz,
        self->integralDeadbandHz, self->mapTrimEnabled ? 1U : 0U,
        (unsigned long)self->mapTrimTransfers, self->mapTrim[5],
        self->mapTrim[9], self->mapTrim[10], self->motionBoostUp,
        self->motionBoostDown, self->scanRunning ? 1U : 0U,
        (unsigned long)self->scanFrameNs, (unsigned long)self->scanBlocks,
        (unsigned long)self->scanGaps);
    adjustometer_reading_t telemetry;
    const bool extendedFresh = getVP37AdjustometerExtendedTelemetry(&telemetry);
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
