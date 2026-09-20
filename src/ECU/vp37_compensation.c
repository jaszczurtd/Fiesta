// VP37 command multipliers: the supply voltage, the fuel-temperature model,
// the measured drive-path resistance and the rate-limited handover between
// them, fed by the shunt scan that vp37_current.c reduces.

#include "vp37_internal.h"
#include <math.h>

#define VP37_LOCAL_VOLTAGE_VALID_MIN_V 5.0f
// Upper end of the calibrated range. A reading above it still scales the
// command: the divider saturates near 18.8 V, so a saturated reading is a
// lower bound of the rail and can only reduce drive. It never trains the
// scale. The 15 V dual-fault value is for a missing reading, not a high one.
#define VP37_LOCAL_VOLTAGE_VALID_MAX_V 17.0f
#define VP37_LOCAL_VOLTAGE_SCALE_FILTER_S 1.0f
#define VP37_LOCAL_VOLTAGE_STABLE_DELTA_V 0.1f

static float VP37_getCompensationInputVoltage(VP37Pump *self, float dt);
static float VP37_predictSupplyVoltage(VP37Pump *self, float measuredVolts);
static void VP37_trackDriveSupply(VP37Pump *self);

hal_status_t VP37_serviceCurrentScan(VP37Pump *self) {
  self->scan.running = hal_adc_scan_is_running();
  self->scan.frameNs = VP37_currentScanFrameNs();
  if (!self->scan.running) {
    return HAL_ESTATE;
  }
  VP37CurrentPulseResult result;
  uint32_t sequence = 0U;
  const hal_status_t status = VP37_currentScanCollect(&result, &sequence);
  if (sequence == 0U) {
    return status;
  }
  if ((self->scan.lastSequence != 0U) &&
      (sequence > (self->scan.lastSequence + 1U))) {
    self->scan.gaps += sequence - self->scan.lastSequence - 1U;
  }
  self->scan.lastSequence = sequence;
  self->scan.blocks++;
  self->scan.cycleResult = result;
  self->scan.cycleResultStatus = status;
  self->scan.cycleResultSequence++;
  if (!self->thermal.observationEnabled || self->demand.atRest ||
      (self->output.finalPWM <= 0)) {
    return status;
  }
  const bool currentUsable = (status == HAL_OK) && result.waveformValid;
  self->supply.cycleVolts = result.supplyLatestVolts;
  self->supply.cycleUs = result.supplyLatestUs;
  self->supply.cycleValid = result.supplyLatestValid;
  // Resistance needs voltage from the current's own period, not the newer
  // supply window. The delivered command is also checked against measured duty.
  self->thermal.cycleValid = currentUsable;
  self->thermal.cycleAmps = currentUsable ? result.meanAmps : 0.0f;
  self->thermal.cycleVolts = result.supplyValid ? result.supplyVolts : 0.0f;
  self->thermal.cyclePwm = result.pwmCommand;
  self->thermal.cycleDrive = self->output.finalPWM;
  self->thermal.cycleUs = result.cycleStartUs + result.periodUs;
  return status;
}

void VP37_updateVoltageCorrection(VP37Pump *self, float dt) {
  float measuredVolts = VP37_getCompensationInputVoltage(self, dt);
  if (measuredVolts < VP37_MIN_COMPENSATION_VOLTAGE) {
    measuredVolts = VP37_MIN_COMPENSATION_VOLTAGE;
  }
  self->supply.inputVolts = measuredVolts;
  const float predictedVolts = VP37_predictSupplyVoltage(self, measuredVolts);
  if (!self->supply.ready) {
    self->supply.heldVolts = measuredVolts;
    self->supply.ready = true;
  } else if (self->supply.frozen) {
    // Diagnostic: keep the scale where it is so the supply loop stays open.
  } else if (self->supply.cycleUsed) {
    self->supply.heldVolts = predictedVolts;
  } else {
    // The local fallback is a 40 us snapshot; only that path needs smoothing.
    self->supply.heldVolts += (measuredVolts - self->supply.heldVolts) * dt /
                              (VP37_VOLTAGE_FILTER_S + dt);
  }
  self->supply.correction = NOMINAL_VOLTAGE / self->supply.heldVolts;
}

/* Predict only the command scale. Resistance learning keeps the measured rail.
   RP2040 PWM latches at wrap; half a period is its average remaining delay. */
static float VP37_predictSupplyVoltage(VP37Pump *self, float measuredVolts) {
  self->supply.predictionVolts = 0.0f;
  self->supply.cycleAgeUs = 0U;
  if (!self->supply.cycleUsed || self->supply.frozen) {
    self->supply.predictionReady = false;
    self->supply.voltageSlope = 0.0f;
  } else {
    const uint32_t elapsedUs =
        self->supply.cycleUs - self->supply.predictionSampleUs;
    if (!self->supply.predictionReady ||
        (elapsedUs >= VP37_CYCLE_VOLTAGE_MAX_AGE_US)) {
      self->supply.voltageSlope = 0.0f;
    } else if (elapsedUs != 0U) {
      const float sampleDt = (float)elapsedUs * 0.000001f;
      const float slope =
          (measuredVolts - self->supply.predictionSampleVolts) / sampleDt;
      const float filtered = self->supply.voltageSlope +
                             (slope - self->supply.voltageSlope) * sampleDt /
                                 (VP37_VOLTAGE_SLOPE_FILTER_S + sampleDt);
      // Drop the lead promptly when a ramp stops or reverses. Filtering only
      // its growth avoids a false pulse after a supply step has finished.
      self->supply.voltageSlope =
          hal_constrain(filtered, fminf(0.0f, slope), fmaxf(0.0f, slope));
    } else {
      // A repeated window advances the prediction horizon, not the slope.
    }
    if (!self->supply.predictionReady || (elapsedUs != 0U)) {
      self->supply.predictionSampleUs = self->supply.cycleUs;
      self->supply.predictionSampleVolts = measuredVolts;
      self->supply.predictionReady = true;
    }
    self->supply.cycleAgeUs = hal_micros() - self->supply.cycleUs;
    const float horizon = ((float)self->supply.cycleAgeUs * 0.000001f) +
                          (0.5f / (float)VP37_PWM_FREQUENCY_HZ);
    self->supply.predictionVolts = hal_constrain(
        self->supply.voltageSlope * horizon, -VP37_VOLTAGE_PREDICTION_LIMIT_V,
        VP37_VOLTAGE_PREDICTION_LIMIT_V);
  }
  return fmaxf(VP37_MIN_COMPENSATION_VOLTAGE,
               measuredVolts + self->supply.predictionVolts);
}

static float VP37_getCompensationInputVoltage(VP37Pump *self, float dt) {
  const bool quantityAtRest = VP37_demandAtRest(self);
  self->supply.cycleUsed =
      self->supply.cycleEnabled && self->thermal.observationEnabled &&
      !quantityAtRest && self->supply.cycleValid &&
      isfinite(self->supply.cycleVolts) &&
      (self->supply.cycleVolts >= VP37_LOCAL_VOLTAGE_VALID_MIN_V) &&
      !hal_elapsed_u32(hal_micros(), self->supply.cycleUs,
                       VP37_CYCLE_VOLTAGE_MAX_AGE_US);
  const float localVolts = self->supply.cycleUsed ? self->supply.cycleVolts
                                                  : self->supply.localVolts;
  const bool localMeasured =
      isfinite(localVolts) && (localVolts >= VP37_LOCAL_VOLTAGE_VALID_MIN_V);
  const bool localValid =
      localMeasured && (localVolts <= VP37_LOCAL_VOLTAGE_VALID_MAX_V);
  self->supply.overRange = localMeasured && !localValid;
  const bool adjustometerVoltageValid =
      (self->feedback.lastStatus & ADJ_STATUS_VOLTAGE_BAD) == 0U;
  float resultVolts;

  if (!localMeasured) {
    self->supply.localReady = false;
    resultVolts = adjustometerVoltageValid ? self->supply.lastVolts
                                           : VP37_MAX_EXPECTED_SUPPLY_VOLTAGE;
  } else if (self->supply.overRange) {
    // Above the calibrated range: scale with the last learned factor and
    // leave the scale alone; the Adjustometer flags its own reading bad here.
    resultVolts = localVolts * self->supply.localScale;
  } else {
    const float scaleTarget = hal_constrain(self->supply.lastVolts / localVolts,
                                            VP37_LOCAL_VOLTAGE_SCALE_MIN,
                                            VP37_LOCAL_VOLTAGE_SCALE_MAX);
    if (!self->supply.localReady) {
      if (adjustometerVoltageValid) {
        self->supply.localScale = scaleTarget;
      }
      self->supply.localReady = true;
    } else {
      const float localDelta =
          fabsf(localVolts - self->supply.previousLocalVolts);
      if (quantityAtRest && adjustometerVoltageValid &&
          (localDelta <= VP37_LOCAL_VOLTAGE_STABLE_DELTA_V)) {
        self->supply.localScale += (scaleTarget - self->supply.localScale) *
                                   dt /
                                   (VP37_LOCAL_VOLTAGE_SCALE_FILTER_S + dt);
      }
    }
    self->supply.previousLocalVolts = localVolts;
    resultVolts = localVolts * self->supply.localScale;
  }
  return resultVolts;
}

void VP37_updateTemperatureCorrection(VP37Pump *self, float dt) {
  const uint8_t invalid = ADJ_STATUS_SIGNAL_LOST | ADJ_STATUS_FUEL_TEMP_BROKEN |
                          ADJ_STATUS_BASELINE_PENDING;
  if (!isfinite(self->thermal.lastFuelTemp) ||
      self->thermal.lastFuelTemp < 0.0f ||
      self->thermal.lastFuelTemp > VP37_THERMAL_TEMP_VALID_MAX_C ||
      (self->feedback.lastStatus & invalid) != 0U) {
    return; // Keep the last valid factor; initialization uses unity.
  }
  const float reference =
      1.0f + VP37_COPPER_TEMP_COEFFICIENT *
                 (VP37_PWM_REFERENCE_TEMP_C - VP37_THERMAL_REFERENCE_TEMP_C);
  const float resistance =
      1.0f + VP37_COPPER_TEMP_COEFFICIENT *
                 (self->thermal.lastFuelTemp - VP37_THERMAL_REFERENCE_TEMP_C);
  const float factor =
      hal_constrain(resistance / reference, VP37_TEMPERATURE_FACTOR_MIN,
                    VP37_TEMPERATURE_FACTOR_MAX);
  const float target =
      1.0f + self->thermal.temperatureCompensationWeight * (factor - 1.0f);
  if (!self->thermal.temperatureReady) {
    self->thermal.temperatureCorrection = target;
    self->thermal.temperatureReady = true;
  } else {
    self->thermal.temperatureCorrection +=
        (target - self->thermal.temperatureCorrection) * dt /
        (VP37_TEMPERATURE_FILTER_S + dt);
  }
}

/**
 * @brief Track the drive-path resistance the shunt capture actually sees.
 * @param self VP37 controller instance to update.
 * @param dt Control period in seconds.
 * @note Coil self-heating moves the required command by several percent while
 * the fuel temperature barely changes, so the measured ratio replaces the
 * fuel-temperature model. A rejected or stale capture keeps the last value and
 * never contributes zero ohms.
 */
void VP37_updateDriveCorrection(VP37Pump *self, float dt) {
  VP37_trackDriveSupply(self);
  // A ready estimate keeps scaling the command until it goes stale; motion
  // rejects most captures, and flipping back to the model on every rejected
  // cycle stepped the command by the whole thermal difference.
  self->thermal.driveCompensationUsed =
      self->thermal.driveCompensationEnabled &&
      self->thermal.driveResistanceReady &&
      !hal_millis_deadline_expired(self->thermal.driveObservedMs,
                                   VP37_DRIVE_STALE_MS);
  if (!self->thermal.driveCompensationEnabled || !self->thermal.cycleValid ||
      !isfinite(self->thermal.cycleAmps) ||
      !isfinite(self->thermal.cycleVolts)) {
    return;
  }
  if (hal_elapsed_u32(hal_micros(), self->thermal.cycleUs,
                      VP37_DRIVE_MAX_AGE_US) ||
      (self->thermal.driveCycleSeen &&
       (self->thermal.cycleUs == self->thermal.driveLastCycleUs))) {
    return;
  }
  const uint32_t observationUs =
      self->thermal.cycleUs - self->thermal.driveLastCycleUs;
  const float observationDt =
      self->thermal.driveCycleSeen ? ((float)observationUs * 0.000001f) : dt;
  const float filterDt = fminf(observationDt, VP37_DRIVE_FILTER_MAX_STEP_S);
  self->thermal.driveLastCycleUs = self->thermal.cycleUs;
  self->thermal.driveCycleSeen = true;
  const int32_t drive = self->thermal.cycleDrive;
  if ((drive < VP37_DRIVE_MIN_PWM) || (drive > VP37_PWM_MAX) ||
      (self->thermal.cycleAmps < VP37_DRIVE_MIN_CURRENT_A) ||
      (self->thermal.cycleVolts < VP37_LOCAL_VOLTAGE_VALID_MIN_V) ||
      (self->thermal.cycleVolts > VP37_LOCAL_VOLTAGE_VALID_MAX_V)) {
    return;
  }
  // Reject a capture that belongs to a different command than the live one.
  const int32_t commandDelta = drive - self->output.finalPWM;
  const int32_t gateDelta = self->thermal.cyclePwm - drive;
  if ((commandDelta > VP37_DRIVE_COMMAND_MATCH_COUNTS) ||
      (commandDelta < -VP37_DRIVE_COMMAND_MATCH_COUNTS) ||
      (gateDelta > VP37_DRIVE_PWM_MATCH_COUNTS) ||
      (gateDelta < -VP37_DRIVE_PWM_MATCH_COUNTS)) {
    return;
  }

  const float duty = (float)drive / (float)PWM_RESOLUTION;
  const float resistance =
      (duty * self->thermal.cycleVolts) / self->thermal.cycleAmps;
  if (!isfinite(resistance) || (resistance <= 0.0f)) {
    return;
  }
  // Healthy captures keep the retained estimate alive during a long supply
  // sweep, even while their electrical transient must not train resistance.
  self->thermal.driveObservedMs = hal_millis();
  if (self->thermal.driveVoltageSettled) {
    // Start from the reference: a first capture during motion can reconstruct
    // a resistance that does not belong to the coil.
    if (!(self->thermal.driveResistance > 0.0f)) {
      self->thermal.driveResistance = VP37_DRIVE_REFERENCE_OHMS;
    }
    self->thermal.driveResistance +=
        (resistance - self->thermal.driveResistance) * filterDt /
        (VP37_DRIVE_FILTER_S + filterDt);
    if (self->thermal.driveSamples == 0U) {
      self->thermal.driveFirstSampleMs = hal_millis();
    }
    if (self->thermal.driveSamples < VP37_DRIVE_READY_SAMPLES) {
      self->thermal.driveSamples++;
    }
    self->thermal.driveUpdatedMs = hal_millis();
    self->thermal.driveResistanceReady =
        (self->thermal.driveSamples >= VP37_DRIVE_READY_SAMPLES) &&
        hal_millis_deadline_expired(self->thermal.driveFirstSampleMs,
                                    VP37_DRIVE_SETTLE_MS);
    self->thermal.driveCorrection =
        hal_constrain(self->thermal.driveResistance / VP37_DRIVE_REFERENCE_OHMS,
                      VP37_TEMPERATURE_FACTOR_MIN, VP37_TEMPERATURE_FACTOR_MAX);
  }
  self->thermal.driveCompensationUsed =
      self->thermal.driveCompensationEnabled &&
      self->thermal.driveResistanceReady;
}

static void VP37_trackDriveSupply(VP37Pump *self) {
  const float volts = self->supply.inputVolts;
  if (!self->thermal.driveVoltageReady ||
      (fabsf(volts - self->thermal.driveVoltageReference) >
       VP37_DRIVE_VOLTAGE_CHANGE_V)) {
    self->thermal.driveVoltageReference = volts;
    self->thermal.driveVoltageChangedMs = hal_millis();
    self->thermal.driveVoltageReady = true;
  }
  // A changing rail and its current transient must not train the slow thermal
  // estimate. Keep using the last ready estimate while observations pause.
  self->thermal.driveVoltageSettled =
      hal_elapsed_u32(hal_millis(), self->thermal.driveVoltageChangedMs,
                      VP37_DRIVE_VOLTAGE_SETTLE_MS);
}

/**
 * @brief Slew the thermal multiplier toward whichever source is in force.
 * @param self VP37 controller instance to update.
 * @param dt Control period in seconds.
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
void VP37_updateThermalScale(VP37Pump *self, float dt) {
  const float target = self->thermal.driveCompensationUsed
                           ? self->thermal.driveCorrection
                           : self->thermal.temperatureCorrection;
  if (!self->thermal.scaleReady) {
    self->thermal.scale = target;
    self->thermal.scaleReady = true;
  } else {
    const float step = VP37_THERMAL_SCALE_SLEW_PER_S * dt;
    const float delta = target - self->thermal.scale;
    if (delta > step) {
      self->thermal.scale += step;
    } else if (delta < -step) {
      self->thermal.scale -= step;
    } else {
      self->thermal.scale = target;
    }
  }
}
