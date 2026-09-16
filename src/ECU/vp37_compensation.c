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
#define VP37_LOCAL_VOLTAGE_SCALE_MIN 0.8f
#define VP37_LOCAL_VOLTAGE_SCALE_MAX 1.2f
#define VP37_LOCAL_VOLTAGE_SCALE_FILTER_S 1.0f
#define VP37_LOCAL_VOLTAGE_STABLE_DELTA_V 0.1f

static float VP37_getCompensationInputVoltage(VP37Pump *self, float dt);

hal_status_t VP37_serviceCurrentScan(VP37Pump *self) {
  self->scan.scanRunning = hal_adc_scan_is_running();
  self->scan.scanFrameNs = VP37_currentScanFrameNs();
  if (!self->scan.scanRunning) {
    return HAL_ESTATE;
  }
  VP37CurrentPulseResult result;
  uint32_t sequence = 0U;
  const hal_status_t status = VP37_currentScanCollect(&result, &sequence);
  if (sequence == 0U) {
    return status;
  }
  if ((self->scan.scanLastSequence != 0U) &&
      (sequence > (self->scan.scanLastSequence + 1U))) {
    self->scan.scanGaps += sequence - self->scan.scanLastSequence - 1U;
  }
  self->scan.scanLastSequence = sequence;
  self->scan.scanBlocks++;
  self->scan.cycleResult = result;
  self->scan.cycleResultStatus = status;
  self->scan.cycleResultSequence++;
  if (!self->thermal.currentObservationEnabled || self->demand.quantityAtRest ||
      (self->output.finalPWM <= 0)) {
    return status;
  }
  const bool currentUsable = (status == HAL_OK) && result.waveformValid;
  self->supply.cycleSupplyVolts = result.supplyVolts;
  self->supply.cycleSupplyUs = result.cycleStartUs + result.periodUs;
  self->supply.cycleSupplyValid = result.supplyValid;
  // The drive command below belongs to the observation, not to its later use.
  self->thermal.cycleCurrentValid = currentUsable;
  self->thermal.cycleCurrentAmps = currentUsable ? result.meanAmps : 0.0f;
  self->thermal.cycleCurrentVolts =
      result.supplyValid ? result.supplyVolts : 0.0f;
  self->thermal.cycleCurrentPwm = result.pwmCommand;
  self->thermal.cycleCurrentDrive = self->output.finalPWM;
  self->thermal.cycleCurrentUs = result.cycleStartUs + result.periodUs;
  return status;
}

void VP37_updateVoltageCorrection(VP37Pump *self, float dt) {
  float measuredVolts = VP37_getCompensationInputVoltage(self, dt);
  if (measuredVolts < VP37_MIN_COMPENSATION_VOLTAGE) {
    measuredVolts = VP37_MIN_COMPENSATION_VOLTAGE;
  }
  self->supply.compensationInputVolts = measuredVolts;
  if (!self->supply.voltageReady) {
    self->supply.compensationVolts = measuredVolts;
    self->supply.voltageReady = true;
  } else if (self->supply.voltageFrozen) {
    // Diagnostic: keep the scale where it is so the supply loop stays open.
  } else if (self->supply.cycleVoltageUsed) {
    // The full-period mean is already free of the intra-period alias, so the
    // scale takes it as is: a manual 15-20 V/s sweep left 0.75-1.3 V behind
    // the 50 ms filter, and cranking edges are faster still.
    self->supply.compensationVolts = measuredVolts;
  } else {
    // The local fallback is a 40 us snapshot; only that path needs smoothing.
    self->supply.compensationVolts +=
        (measuredVolts - self->supply.compensationVolts) * dt /
        (VP37_VOLTAGE_FILTER_S + dt);
  }
  self->supply.voltageCorrection =
      NOMINAL_VOLTAGE / self->supply.compensationVolts;
}

static float VP37_getCompensationInputVoltage(VP37Pump *self, float dt) {
  const bool quantityAtRest = VP37_demandAtRest(self);
  self->supply.cycleVoltageUsed =
      self->supply.cycleVoltageEnabled &&
      self->thermal.currentObservationEnabled && !quantityAtRest &&
      self->supply.cycleSupplyValid &&
      isfinite(self->supply.cycleSupplyVolts) &&
      (self->supply.cycleSupplyVolts >= VP37_LOCAL_VOLTAGE_VALID_MIN_V) &&
      !hal_elapsed_u32(hal_micros(), self->supply.cycleSupplyUs,
                       VP37_CYCLE_VOLTAGE_MAX_AGE_US);
  const float localVolts = self->supply.cycleVoltageUsed
                               ? self->supply.cycleSupplyVolts
                               : self->supply.localVolts;
  const bool localMeasured =
      isfinite(localVolts) && (localVolts >= VP37_LOCAL_VOLTAGE_VALID_MIN_V);
  const bool localValid =
      localMeasured && (localVolts <= VP37_LOCAL_VOLTAGE_VALID_MAX_V);
  self->supply.voltageOverRange = localMeasured && !localValid;
  const bool adjustometerVoltageValid =
      (self->feedback.lastAdjustometerStatus & ADJ_STATUS_VOLTAGE_BAD) == 0U;
  float resultVolts;

  if (!localMeasured) {
    self->supply.localVoltageReady = false;
    resultVolts = adjustometerVoltageValid ? self->supply.lastVolts
                                           : VP37_MAX_EXPECTED_SUPPLY_VOLTAGE;
  } else if (self->supply.voltageOverRange) {
    // Above the calibrated range: scale with the last learned factor and
    // leave the scale alone; the Adjustometer flags its own reading bad here.
    resultVolts = localVolts * self->supply.localVoltageScale;
  } else {
    const float scaleTarget = hal_constrain(self->supply.lastVolts / localVolts,
                                            VP37_LOCAL_VOLTAGE_SCALE_MIN,
                                            VP37_LOCAL_VOLTAGE_SCALE_MAX);
    if (!self->supply.localVoltageReady) {
      if (adjustometerVoltageValid) {
        self->supply.localVoltageScale = scaleTarget;
      }
      self->supply.localVoltageReady = true;
    } else {
      const float localDelta =
          fabsf(localVolts - self->supply.previousLocalVolts);
      if (quantityAtRest && adjustometerVoltageValid &&
          (localDelta <= VP37_LOCAL_VOLTAGE_STABLE_DELTA_V)) {
        self->supply.localVoltageScale +=
            (scaleTarget - self->supply.localVoltageScale) * dt /
            (VP37_LOCAL_VOLTAGE_SCALE_FILTER_S + dt);
      }
    }
    self->supply.previousLocalVolts = localVolts;
    resultVolts = localVolts * self->supply.localVoltageScale;
  }
  return resultVolts;
}

void VP37_updateTemperatureCorrection(VP37Pump *self, float dt) {
  const uint8_t invalid = ADJ_STATUS_SIGNAL_LOST | ADJ_STATUS_FUEL_TEMP_BROKEN |
                          ADJ_STATUS_BASELINE_PENDING;
  if (!isfinite(self->thermal.lastFuelTemp) ||
      self->thermal.lastFuelTemp < 0.0f ||
      self->thermal.lastFuelTemp > VP37_THERMAL_TEMP_VALID_MAX_C ||
      (self->feedback.lastAdjustometerStatus & invalid) != 0U) {
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
 * @return None.
 * @note Coil self-heating moves the required command by several percent while
 * the fuel temperature barely changes, so the measured ratio replaces the
 * fuel-temperature model. A rejected or stale capture keeps the last value and
 * never contributes zero ohms.
 */
void VP37_updateDriveCorrection(VP37Pump *self, float dt) {
  // A ready estimate keeps scaling the command until it goes stale; motion
  // rejects most captures, and flipping back to the model on every rejected
  // cycle stepped the command by the whole thermal difference.
  self->thermal.driveCompensationUsed =
      self->thermal.driveCompensationEnabled &&
      self->thermal.driveResistanceReady &&
      !hal_millis_deadline_expired(self->thermal.driveUpdatedMs,
                                   VP37_DRIVE_STALE_MS);
  if (!self->thermal.driveCompensationEnabled ||
      !self->thermal.cycleCurrentValid ||
      !isfinite(self->thermal.cycleCurrentAmps) ||
      !isfinite(self->thermal.cycleCurrentVolts)) {
    return;
  }
  if (hal_elapsed_u32(hal_micros(), self->thermal.cycleCurrentUs,
                      VP37_DRIVE_MAX_AGE_US)) {
    return;
  }
  const int32_t drive = self->thermal.cycleCurrentDrive;
  if ((drive < VP37_DRIVE_MIN_PWM) || (drive > VP37_PWM_MAX) ||
      (self->thermal.cycleCurrentAmps < VP37_DRIVE_MIN_CURRENT_A) ||
      (self->thermal.cycleCurrentVolts < VP37_LOCAL_VOLTAGE_VALID_MIN_V) ||
      (self->thermal.cycleCurrentVolts > VP37_LOCAL_VOLTAGE_VALID_MAX_V)) {
    return;
  }
  // Reject a capture that belongs to a different command than the live one.
  const int32_t commandDelta = drive - self->output.finalPWM;
  const int32_t gateDelta = self->thermal.cycleCurrentPwm - drive;
  if ((commandDelta > VP37_DRIVE_COMMAND_MATCH_COUNTS) ||
      (commandDelta < -VP37_DRIVE_COMMAND_MATCH_COUNTS) ||
      (gateDelta > VP37_DRIVE_PWM_MATCH_COUNTS) ||
      (gateDelta < -VP37_DRIVE_PWM_MATCH_COUNTS)) {
    return;
  }

  const float duty = (float)drive / (float)PWM_RESOLUTION;
  const float resistance =
      (duty * self->thermal.cycleCurrentVolts) / self->thermal.cycleCurrentAmps;
  if (!isfinite(resistance) || (resistance <= 0.0f)) {
    return;
  }
  // Always filter from the reference. Seeding from the first accepted capture
  // used to adopt it whole, and a capture taken while the actuator is slamming
  // through its stroke reconstructs a resistance that is not the coil's.
  if (!(self->thermal.driveResistance > 0.0f)) {
    self->thermal.driveResistance = VP37_DRIVE_REFERENCE_OHMS;
  }
  self->thermal.driveResistance +=
      (resistance - self->thermal.driveResistance) * dt /
      (VP37_DRIVE_FILTER_S + dt);
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
  self->thermal.driveCompensationUsed =
      self->thermal.driveCompensationEnabled &&
      self->thermal.driveResistanceReady;
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
void VP37_updateThermalScale(VP37Pump *self, float dt) {
  const float target = self->thermal.driveCompensationUsed
                           ? self->thermal.driveCorrection
                           : self->thermal.temperatureCorrection;
  if (!self->thermal.thermalScaleReady) {
    self->thermal.thermalScale = target;
    self->thermal.thermalScaleReady = true;
  } else {
    const float step = VP37_THERMAL_SCALE_SLEW_PER_S * dt;
    const float delta = target - self->thermal.thermalScale;
    if (delta > step) {
      self->thermal.thermalScale += step;
    } else if (delta < -step) {
      self->thermal.thermalScale -= step;
    } else {
      self->thermal.thermalScale = target;
    }
  }
}
