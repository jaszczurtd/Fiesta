// VP37 correction loop: the feedforward from the holding map, the learned map
// trim, and the authority, dead zone and hold rules the PID runs under.

#include "vp37_internal.h"
#include <math.h>
#include <string.h>

static float VP37_strokePercent(const VP37Pump *self, float position);
TESTABLE_STATIC float VP37_strokeTaper(const float *knots, size_t count,
                                       float percent);
static float VP37_mapTrimAt(const VP37Pump *self, float percent);
static void VP37_mapTrimKnots(float percent, uint32_t *lower, float *weight);

void VP37_resetMotionIntegralGate(VP37Pump *self) {
  self->pid.motionIntegralReady = false;
  self->pid.motionIntegralHold = false;
  self->pid.motionIntegralPosition = 0;
  self->pid.motionIntegralTarget = 0;
  self->pid.motionIntegralBias = 0.0f;
  self->pid.motionIntegralAnchor = 0.0f;
  self->pid.motionIntegralStartPosition = 0;
  self->pid.descentUnwindTarget = 0.0f;
  self->pid.descentUnwindPhase = VP37_DESCENT_UNWIND_WAITING;
  self->pid.holdingIntegralTarget = 0;
  self->pid.holdingIntegralBias = 0.0f;
  self->pid.holdingIntegralCandidate = 0.0f;
  self->pid.holdingIntegralElapsed = 0.0f;
  self->pid.holdingIntegralReady = false;
  self->pid.descentUnwindUsesHolding = false;
  self->pid.motionIntegralVelocity = 0.0f;
}

bool HAL_RAM_FUNC(VP37_hasMeasuredDescent)(const VP37Pump *self) {
  const float stroke =
      (float)self->feedback.adjustMax - (float)self->feedback.adjustMin;
  const float travel = (float)self->pid.motionIntegralStartPosition -
                       (float)self->feedback.position;
  return (stroke > 0.0f) && (travel >= 0.003f * stroke);
}

/** @brief Observe a holding correction without changing the controller output.
 */
static void HAL_RAM_FUNC(VP37_observeHoldingIntegral)(VP37Pump *self,
                                                      bool settled, float dt) {
  const float integral = self->pid.terms.integral;
  const bool qualified =
      self->pid.researchDescentIntegralUnwind && settled &&
      (self->demand.target > self->feedback.adjustMin) &&
      (self->demand.target < self->feedback.adjustMax) &&
      (fabsf((float)self->pid.error) <= self->pid.integralDeadbandHz) &&
      isfinite(integral) && (fabsf(integral) < self->pid.integralLimit) &&
      !self->pid.terms.saturated_low && !self->pid.terms.saturated_high &&
      (self->output.finalPWM > VP37_PWM_MIN) &&
      (self->output.finalPWM < VP37_PWM_MAX) &&
      ((self->pid.researchFreezeMask & VP37_RESEARCH_FREEZE_INTEGRAL_MASK) ==
       0U);
  if (!qualified || !isfinite(self->pid.holdingIntegralCandidate) ||
      (fabsf(integral - self->pid.holdingIntegralCandidate) >
       VP37_HOLDING_BIAS_MAX_DRIFT_PWM)) {
    self->pid.holdingIntegralElapsed = 0.0f;
    self->pid.holdingIntegralCandidate = integral;
  } else {
    self->pid.holdingIntegralElapsed = fminf(
        self->pid.holdingIntegralElapsed + dt, VP37_HOLDING_BIAS_CONFIRM_S);
    if (self->pid.holdingIntegralElapsed >= VP37_HOLDING_BIAS_CONFIRM_S) {
      self->pid.holdingIntegralTarget = self->demand.target;
      self->pid.holdingIntegralBias = integral;
      self->pid.holdingIntegralReady = true;
    }
  }
}

bool HAL_RAM_FUNC(VP37_motionIntegralHold)(VP37Pump *self, bool settled,
                                           float dt) {
  if (!self->pid.fullRangePrototype || !self->pid.researchMotionIntegralGate ||
      !isfinite(dt) || (dt <= 0.0f) || (dt > 0.15f)) {
    VP37_resetMotionIntegralGate(self);
    return false;
  }
  const int32_t position = self->feedback.position;
  const bool newTarget = !self->pid.motionIntegralReady ||
                         self->pid.motionIntegralTarget != self->demand.target;
  const float previousError =
      (float)self->demand.target - (float)self->pid.motionIntegralPosition;
  if (newTarget) {
    const bool fromMaximum =
        (position >= self->feedback.adjustMax) ||
        (self->pid.motionIntegralReady &&
         (self->pid.motionIntegralTarget >= self->feedback.adjustMax));
    self->pid.motionIntegralTarget = self->demand.target;
    self->pid.motionIntegralBias = self->pid.terms.integral;
    // At the endpoint, I may retain arbitrary breakaway drive. It cannot
    // identify the next holding correction; use the nominal map's zero.
    self->pid.motionIntegralAnchor =
        fromMaximum ? 0.0f : self->pid.terms.integral;
    self->pid.motionIntegralStartPosition = position;
    self->pid.descentUnwindTarget = self->pid.terms.integral;
    self->pid.descentUnwindPhase = VP37_DESCENT_UNWIND_WAITING;
    self->pid.descentUnwindUsesHolding = false;
    self->pid.holdingIntegralElapsed = 0.0f;
    self->pid.holdingIntegralCandidate = self->pid.terms.integral;
  }
  if (self->pid.motionIntegralReady) {
    const float velocity =
        ((float)position - (float)self->pid.motionIntegralPosition) / dt;
    const float alpha = dt / (VP37_MOTION_INTEGRAL_FILTER_S + dt);
    self->pid.motionIntegralVelocity +=
        alpha * (velocity - self->pid.motionIntegralVelocity);
  } else {
    self->pid.motionIntegralVelocity = 0.0f;
    self->pid.motionIntegralReady = true;
  }
  self->pid.motionIntegralPosition = position;
  const float error = (float)self->pid.error;
  const float threshold = self->pid.motionIntegralHold
                              ? VP37_MOTION_INTEGRAL_EXIT_HZ_S
                              : VP37_MOTION_INTEGRAL_ENTER_HZ_S;
  // Retain the holding bias during approach. At rest it may build; motion
  // away from the goal permits integration to brake an overshoot.
  self->pid.motionIntegralHold =
      settled && (fabsf(error) > self->pid.integralDeadbandHz) &&
      ((self->pid.motionIntegralVelocity * error) > 0.0f) &&
      (fabsf(self->pid.motionIntegralVelocity) > threshold);
  VP37_observeHoldingIntegral(self, settled && !newTarget, dt);
  if (self->pid.researchDescentIntegralUnwind && settled && !newTarget) {
    if ((fabsf(error) <= self->pid.integralDeadbandHz) ||
        ((previousError * error) <= 0.0f)) {
      self->pid.descentUnwindPhase = VP37_DESCENT_UNWIND_ARRIVED;
    } else if ((self->pid.descentUnwindPhase == VP37_DESCENT_UNWIND_WAITING) &&
               self->pid.motionIntegralHold && (error < 0.0f) &&
               !self->pid.integralHold &&
               ((self->pid.researchFreezeMask &
                 VP37_RESEARCH_FREEZE_INTEGRAL_MASK) == 0U)) {
      const float acquired =
          self->pid.terms.integral - self->pid.motionIntegralAnchor;
      if (VP37_hasMeasuredDescent(self) && (acquired < 0.0f)) {
        // Retain the declared share for the new holding position.
        self->pid.descentUnwindUsesHolding =
            self->pid.holdingIntegralReady &&
            (self->pid.holdingIntegralTarget == self->demand.target);
        if (self->pid.descentUnwindUsesHolding) {
          // Reuse observed holding drive, without adding negative drive.
          self->pid.descentUnwindTarget = fmaxf(
              self->pid.terms.integral,
              hal_constrain(self->pid.holdingIntegralBias,
                            -self->pid.integralLimit, self->pid.integralLimit));
        } else {
          self->pid.descentUnwindTarget =
              self->pid.motionIntegralAnchor +
              self->pid.descentIntegralRetainedShare * acquired;
        }
        self->pid.descentUnwindPhase = VP37_DESCENT_UNWIND_ACTIVE;
      }
    } else {
      // Active assistance keeps its first target until arrival.
    }
  }
  return self->pid.motionIntegralHold;
}

float VP37_feedForward(VP37Pump *self, int32_t position) {
  // The holding map and its motion column are data in vp37_maps.c; this is
  // only the interpolation between its knots and the blending of motion.
  const float percent = VP37_strokePercent(self, (float)position);
  // Extra acceleration excites the upper stroke; preserve lower-stroke drive.
  const bool reference = self->pid.septemberReference;
  const float(*const map)[VP37_FF_COLUMNS] =
      reference ? VP37_SEPTEMBER_FF_MAP : VP37_FF_MAP;
  const float riseWeight =
      reference ? 1.0f
                : hal_constrain(hal_math_map_f32(
                                    percent, VP37_PWM_FF_MOTION_TAPER_START,
                                    VP37_PWM_FF_MOTION_TAPER_END, 1.0f, 0.0f),
                                0.0f, 1.0f);
  for (size_t i = 1U; i < VP37_FF_KNOTS; ++i) {
    const float *lower = map[i - 1U];
    const float *upper = map[i];
    if (percent <= upper[VP37_FF_COL_PERCENT]) {
      const float holding = hal_math_map_f32(
          percent, lower[VP37_FF_COL_PERCENT], upper[VP37_FF_COL_PERCENT],
          lower[VP37_FF_COL_PWM], upper[VP37_FF_COL_PWM]);
      self->feedforward.motion =
          (self->feedforward.riseBlend * riseWeight *
           hal_math_map_f32(
               percent, lower[VP37_FF_COL_PERCENT], upper[VP37_FF_COL_PERCENT],
               lower[VP37_FF_COL_MOTION], upper[VP37_FF_COL_MOTION]) *
           (self->feedforward.motionBoostUp /
            map[VP37_FF_KNOTS - 1U][VP37_FF_COL_MOTION])) -
          (self->feedforward.fallBlend * self->feedforward.motionBoostDown);
      self->feedforward.mapTrimApplied = VP37_mapTrimAt(self, percent);
      return (holding * VP37_PWM_FF_HARDWARE_GAIN) +
             self->feedforward.mapTrimApplied + self->feedforward.motion;
    }
  }
  self->feedforward.motion = 0.0f;
  self->feedforward.mapTrimApplied = VP37_mapTrimAt(self, 100.0f);
  return (map[VP37_FF_KNOTS - 1U][VP37_FF_COL_PWM] *
          VP37_PWM_FF_HARDWARE_GAIN) +
         self->feedforward.mapTrimApplied;
}

float VP37_computePositiveCorrectionLimit(float fuelTempC,
                                          uint8_t adjustometerStatus,
                                          float pwmFeedForward) {
  const uint8_t invalidTemperatureStatus = ADJ_STATUS_SIGNAL_LOST |
                                           ADJ_STATUS_FUEL_TEMP_BROKEN |
                                           ADJ_STATUS_BASELINE_PENDING;

  if (((adjustometerStatus & invalidTemperatureStatus) != 0U) ||
      (fuelTempC != fuelTempC) || (fuelTempC > VP37_THERMAL_TEMP_VALID_MAX_C) ||
      (fuelTempC <= VP37_THERMAL_REFERENCE_TEMP_C)) {
    return VP37_PID_CORR_LIMIT_POSITIVE_COLD;
  }

  const float coldMaximumCommand =
      pwmFeedForward + VP37_PID_CORR_LIMIT_POSITIVE_COLD;
  const float thermalFactor =
      1.0f + (VP37_COPPER_TEMP_COEFFICIENT *
              (fuelTempC - VP37_THERMAL_REFERENCE_TEMP_C));
  float positiveLimit = (coldMaximumCommand * thermalFactor) - pwmFeedForward;

  return hal_constrain(positiveLimit, VP37_PID_CORR_LIMIT_POSITIVE_COLD,
                       VP37_PID_CORR_LIMIT_POSITIVE_MAX);
}

/**
 * @brief Integral authority for the demanded position.
 * @return Authority in nominal PWM counts.
 * @note The research profile allows the full tuning range; the output limits
 * and PID anti-windup still bound the actual correction. The normal profile
 * retains its position taper. A positive bench cap only lowers either limit.
 */
float VP37_integralLimit(const VP37Pump *self) {
  float limit =
      self->pid.fullRangePrototype
          ? VP37_BENCH_INTEGRAL_LIMIT_MAX
          : VP37_strokeTaper(
                &VP37_INTEGRAL_LIMIT_MAP[0U][0U], VP37_STROKE_TAPER_KNOTS,
                VP37_strokePercent(self, self->demand.desiredPosition));
  if (self->pid.integralOverride > 0.0f) {
    limit = fminf(limit, self->pid.integralOverride);
  }
  return limit;
}

/**
 * @brief Pick the integration dead zone for the demanded position.
 * @return Dead zone in hertz, never below the base value.
 * @note The upper stroke settles hundreds of hertz apart for the same command,
 * so integrating small errors there only winds force against the mechanism.
 * Below the taper start the loop keeps its full accuracy.
 */
float HAL_RAM_FUNC(VP37_integralDeadband)(const VP37Pump *self) {
  const float base = VP37_INTEGRAL_DEADBAND_MAP[0U][VP37_TAPER_COL_VALUE];
  float deadband = base;
  if (self->pid.fullRangePrototype && self->pid.researchUpperDeadband) {
    const float percent =
        VP37_strokePercent(self, self->demand.desiredPosition);
    const float weight = hal_constrain(
        hal_math_map_f32(percent, 90.0f, 95.0f, 0.0f, 1.0f), 0.0f, 1.0f);
    deadband += fmaxf(0.0f, self->pid.integralDeadbandTopHz - base) * weight;
  } else if (!self->pid.septemberReference && !self->pid.fullRangePrototype &&
             (self->pid.integralDeadbandTopHz > base)) {
    // The table holds the default top; the bench may have moved it.
    float taper[VP37_STROKE_TAPER_KNOTS * VP37_STROKE_TAPER_COLUMNS];
    (void)memcpy(taper, VP37_INTEGRAL_DEADBAND_MAP, sizeof(taper));
    taper[((VP37_STROKE_TAPER_KNOTS - 1U) * VP37_STROKE_TAPER_COLUMNS) +
          VP37_TAPER_COL_VALUE] = self->pid.integralDeadbandTopHz;
    deadband = VP37_strokeTaper(
        taper, VP37_STROKE_TAPER_KNOTS,
        VP37_strokePercent(self, self->demand.desiredPosition));
  }
  return deadband;
}

void VP37_updateDerivativeGain(VP37Pump *self, bool targetSettled) {
  const float percent = VP37_strokePercent(self, self->demand.desiredPosition);
  float weight = 0.0f;
  if (((self->pid.topKd > 0.0f) || self->pid.researchSlowDamping) &&
      !self->demand.atRest && (percent > 85.0f)) {
    const float dt = (float)self->pidDtUs * 0.000001f;
    const float alpha = dt / (VP37_PID_TOP_D_BLEND_S + dt);
    if (self->pid.fullRangePrototype && self->pid.researchRampDamping) {
      self->pid.topDBlend = 1.0f;
    } else {
      self->pid.topDBlend = hal_math_low_pass(
          alpha, targetSettled ? 1.0f : 0.0f, self->pid.topDBlend);
    }
    weight = hal_constrain(hal_math_map_f32(percent, 85.0f, 90.0f, 0.0f, 1.0f),
                           0.0f, 1.0f);
  } else {
    self->pid.topDBlend = 0.0f;
  }
  self->pid.effectiveKd =
      self->pid.kd + (self->pid.topKd * weight * self->pid.topDBlend);
  hal_pid_controller_set_kd(self->pid.controller, self->pid.effectiveKd);
}

/**
 * @brief Update the settled-target hysteresis that freezes integration.
 * @param self VP37 controller instance to update.
 * @param targetSettled The demand stands at its target.
 * @note The hold engages after the confirmation time inside the entry band
 * and keeps the integral out of the static-friction band. A large error
 * releases it after VP37_INTEGRAL_HOLD_RELEASE_MS. With the offset release on,
 * the hold engages only when the mean error is inside the entry band too, and
 * where the integral dead zone is the base zone, a mean error that stays
 * outside the entry band also releases it.
 */
void HAL_RAM_FUNC(VP37_updateIntegralHold)(VP37Pump *self, bool targetSettled) {
  if (self->pid.septemberReference) {
    self->pid.integralHold = false;
    self->pid.integralHoldEntered = false;
    return;
  }
  const int32_t error = self->pid.error;
  // Fixed bands on purpose: widening them with the dead zone froze the
  // integral up to twice the dead zone from the target and left standing
  // errors of 120 Hz on the upper stroke. There the dead zone alone bounds
  // the error; the hold matters where the band is narrower than the zone.
  const int32_t enterHz = VP37_INTEGRAL_HOLD_ENTER_HZ;
  const int32_t exitHz = VP37_INTEGRAL_HOLD_EXIT_HZ;
  const bool meanEntry = self->pid.integralHoldOffsetMs > 0U;
  // Only where the integral dead zone is the base zone: above the taper start
  // integration walks until the mechanism breaks free (VP37_PID_DEADBAND), and
  // a 120 s hold at 90 % demand turned the offset release into stick-slip.
  const bool offsetRelease =
      meanEntry && (self->pid.integralDeadbandHz <= (float)VP37_PID_DEADBAND);
  self->pid.integralHoldEntered = false;
  if (!targetSettled) {
    // A moving target starts the mean afresh from the error it leaves.
    self->pid.integralHoldMeanError = (float)error;
    self->pid.integralHold = false;
    self->pid.integralHoldEnterPending = false;
    self->pid.integralHoldReleasePending = false;
    self->pid.integralHoldOffsetPending = false;
    return;
  }
  const float dt = (float)self->pidDtUs * 0.000001f;
  self->pid.integralHoldMeanError +=
      ((float)error - self->pid.integralHoldMeanError) * dt /
      (VP37_INTEGRAL_HOLD_MEAN_FILTER_S + dt);
  const float meanAbs = fabsf(self->pid.integralHoldMeanError);
  const bool meanInBand = meanAbs <= (float)enterHz;
  // Where the offset release applies the mean must come closer before the
  // hold engages than it may stray before the hold releases.
  const bool meanSettled =
      meanAbs <=
      (offsetRelease ? self->pid.integralHoldMeanEnterHz : (float)enterHz);
  if (!self->pid.integralHold) {
    if ((error < -enterHz) || (error > enterHz) ||
        (meanEntry && !meanSettled)) {
      self->pid.integralHoldEnterPending = false;
    } else {
      if (!self->pid.integralHoldEnterPending) {
        self->pid.integralHoldEnterStartedMs = hal_millis();
        self->pid.integralHoldEnterPending = true;
      }
      if (hal_millis_deadline_expired(self->pid.integralHoldEnterStartedMs,
                                      self->pid.integralHoldConfirmMs)) {
        self->pid.integralHold = true;
        self->pid.integralHoldEntered = true;
        self->pid.integralHoldEnterPending = false;
        self->pid.integralHoldReleasePending = false;
        self->pid.integralHoldOffsetPending = false;
      }
    }
  } else {
    const int32_t releaseThresholdHz =
        self->pid.integralHoldReleasePending ? enterHz : exitHz;
    bool release = false;
    // Once armed, only recovery into the settled band cancels release.
    if ((error >= -releaseThresholdHz) && (error <= releaseThresholdHz)) {
      self->pid.integralHoldReleasePending = false;
    } else if (!self->pid.integralHoldReleasePending) {
      self->pid.integralHoldReleasePending = true;
      self->pid.integralHoldReleaseStartedMs = hal_millis();
    } else {
      release =
          hal_millis_deadline_expired(self->pid.integralHoldReleaseStartedMs,
                                      VP37_INTEGRAL_HOLD_RELEASE_MS);
    }
    // A standing offset below the large-error band: only the mean decides.
    if (!offsetRelease || meanInBand) {
      self->pid.integralHoldOffsetPending = false;
    } else if (!self->pid.integralHoldOffsetPending) {
      self->pid.integralHoldOffsetPending = true;
      self->pid.integralHoldOffsetStartedMs = hal_millis();
    } else if (hal_millis_deadline_expired(
                   self->pid.integralHoldOffsetStartedMs,
                   self->pid.integralHoldOffsetMs)) {
      release = true;
    } else {
      // The offset has not persisted long enough yet.
    }
    if (release) {
      self->pid.integralHold = false;
      self->pid.integralHoldEnterPending = false;
      self->pid.integralHoldReleasePending = false;
      self->pid.integralHoldOffsetPending = false;
    }
  }
}

/**
 * @brief Move the settled integral into the learned map trim.
 * @note Runs once when the settled-position hold engages. The trim takes the
 * whole integral and the controller restarts from zero, so this step's output
 * and the next step's feedforward add up to the same command: no bump. A trim
 * that would leave the bound keeps the integral where it is.
 */
void VP37_transferIntegralToMapTrim(VP37Pump *self) {
  if (!self->feedforward.mapTrimEnabled || !self->pid.integralHoldEntered) {
    return;
  }
  self->pid.integralHoldEntered = false;
  const float percent = hal_constrain(
      hal_math_map_f32(self->demand.desiredPosition,
                       (float)self->feedback.adjustMin,
                       (float)self->feedback.adjustMax, 0.0f, 100.0f),
      0.0f, 100.0f);
  uint32_t lower = 0U;
  float weight = 0.0f;
  VP37_mapTrimKnots(percent, &lower, &weight);
  const uint32_t upper =
      ((lower + 1U) < VP37_MAP_TRIM_KNOTS) ? (lower + 1U) : lower;
  const float integral = self->pid.terms.integral;
  // Both knots take the whole integral: the interpolated value then rises by
  // exactly that amount at this position, and neighbouring holds average
  // out the friction share of what each of them learned.
  const float lowerCandidate = self->feedforward.mapTrim[lower] + integral;
  const float upperCandidate = self->feedforward.mapTrim[upper] + integral;
  if (!isfinite(lowerCandidate) || !isfinite(upperCandidate) ||
      (fabsf(lowerCandidate) > VP37_MAP_TRIM_LIMIT_PWM) ||
      (fabsf(upperCandidate) > VP37_MAP_TRIM_LIMIT_PWM) ||
      (fabsf(integral) < 0.5f)) {
    return;
  }
  self->feedforward.mapTrim[lower] = lowerCandidate;
  self->feedforward.mapTrim[upper] = upperCandidate;
  hal_pid_controller_reset(self->pid.controller);
  self->pid.terms.integral = 0.0f;
  self->feedforward.mapTrimTransfers++;
}

/**
 * @brief Demand along the calibrated stroke, the axis every stroke map uses.
 * @param self VP37 controller instance to inspect.
 * @param position Adjustometer position.
 * @return Demand in percent, clamped to the calibrated range.
 */
static float VP37_strokePercent(const VP37Pump *self, float position) {
  return hal_constrain(
      hal_math_map_f32(position, (float)self->feedback.adjustMin,
                       (float)self->feedback.adjustMax, 0.0f, 100.0f),
      0.0f, 100.0f);
}

/**
 * @brief Value of a stroke taper at a demand, both ends held flat.
 * @param knots Rows of {demand [%], value} in ascending demand, laid out
 * row-major as in vp37_maps.h.
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

/** @brief Learned holding-map residual, interpolated between knots. */
static float VP37_mapTrimAt(const VP37Pump *self, float percent) {
  if (!self->feedforward.mapTrimEnabled) {
    return 0.0f;
  }
  uint32_t lower = 0U;
  float weight = 0.0f;
  VP37_mapTrimKnots(percent, &lower, &weight);
  const uint32_t upper =
      ((lower + 1U) < VP37_MAP_TRIM_KNOTS) ? (lower + 1U) : lower;
  return (self->feedforward.mapTrim[lower] * (1.0f - weight)) +
         (self->feedforward.mapTrim[upper] * weight);
}

/**
 * @brief Locate the two learned-trim knots around a stroke percentage.
 * @param percent Stroke position, clamped to 0..100.
 * @param lower Non-NULL; receives the lower knot index.
 * @param weight Non-NULL; receives the upper knot's share, 0..1.
 * @note The last knot pairs with itself.
 */
static void VP37_mapTrimKnots(float percent, uint32_t *lower, float *weight) {
  const uint32_t spans = VP37_MAP_TRIM_KNOTS - 1U;
  const float step = 100.0f / (float)spans;
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

void VP37_resetSlowDamping(VP37Pump *self) {
  self->pid.integralInput = 0.0f;
  self->pid.referenceKp = self->pid.kp;
  self->pid.slowDampingReady = false;
  self->pid.slowDampingPosition = 0;
  self->pid.slowDampingOutput = 0.0f;
  for (size_t i = 0U; i < COUNTOF(self->pid.slowDampingVelocity); ++i) {
    self->pid.slowDampingVelocity[i] = 0.0f;
  }
}

float HAL_RAM_FUNC(VP37_updateSlowDamping)(VP37Pump *self, float dt) {
  if (!self->pid.fullRangePrototype || !self->pid.researchSlowDamping ||
      !isfinite(dt) || (dt <= 0.0f) || (dt > 0.1f)) {
    VP37_resetSlowDamping(self);
    return 0.0f;
  }
  float velocity = 0.0f;
  if (self->pid.slowDampingReady) {
    velocity = ((float)self->feedback.position -
                (float)self->pid.slowDampingPosition) /
               dt;
  } else {
    self->pid.slowDampingReady = true;
  }
  self->pid.slowDampingPosition = self->feedback.position;
  const float alpha = dt / (VP37_SLOW_DAMPING_TF_S + dt);
  for (size_t i = 0U; i < COUNTOF(self->pid.slowDampingVelocity); ++i) {
    self->pid.slowDampingVelocity[i] =
        hal_math_low_pass(alpha, velocity, self->pid.slowDampingVelocity[i]);
    velocity = self->pid.slowDampingVelocity[i];
  }
  const float percent = VP37_strokePercent(self, self->demand.desiredPosition);
  const float weight = hal_constrain(
      hal_math_map_f32(percent, 85.0f, 90.0f, 0.0f, 1.0f), 0.0f, 1.0f);
  self->pid.slowDampingOutput =
      self->pid.researchSlowDamping
          ? -VP37_SLOW_DAMPING_KD * velocity * weight * self->pid.topDBlend
          : 0.0f;
  return self->pid.slowDampingOutput;
}

float VP37_referencePotential(const VP37Pump *self, float position,
                              float *weight) {
  const float span =
      (float)self->feedback.adjustMax - (float)self->feedback.adjustMin;
  float blend = 0.0f;
  float result = 0.0f;
  if (span > 0.0f) {
    const float width = span * 0.05f;
    const float distance =
        position - ((float)self->feedback.adjustMin + span * 0.85f);
    if (distance >= width) {
      blend = 1.0f;
      result = distance - (0.5f * width);
    } else if (distance > 0.0f) {
      blend = distance / width;
      result = 0.5f * distance * blend;
    } else {
      // The original controller remains active below the upper blend.
    }
  }
  if (weight != NULL) {
    *weight = blend;
  }
  return result;
}

hal_status_t VP37_stepDampedCorrection(VP37Pump *self, float dt, float deadband,
                                       int32_t previousDesired) {
  const float extraD = VP37_updateSlowDamping(self, dt);
  const float error = (float)self->pid.error;
  float input = error;
  float integrationBand = deadband;
  self->pid.referenceKp = self->pid.kp;
  if (self->pid.fullRangePrototype &&
      self->pid.researchReferenceWeight < 1.0f) {
    if (!(self->pid.ki > 0.0f) || !isfinite(dt) || !(dt > 0.0f)) {
      return HAL_ESTATE;
    }
    float weight = 0.0f;
    const float potential =
        VP37_referencePotential(self, (float)self->demand.desired, &weight);
    const float previous =
        previousDesired >= 0
            ? VP37_referencePotential(self, (float)previousDesired, NULL)
            : potential;
    const float reduction =
        self->pid.kp * (1.0f - self->pid.researchReferenceWeight);
    // Shift the physical bias only when the reference moves. At a fixed
    // reference this is the ordinary PID, including through the upper blend.
    input = error - hal_constrain(error, -deadband, deadband) -
            reduction * (potential - previous) / (self->pid.ki * dt);
    integrationBand = 0.0f;
    self->pid.referenceKp -= reduction * weight;
  }
  if (self->pid.fullRangePrototype && self->pid.researchDescentIntegralUnwind &&
      self->pid.motionIntegralHold &&
      (self->pid.descentUnwindPhase == VP37_DESCENT_UNWIND_ACTIVE) &&
      !self->pid.integralHold &&
      ((self->pid.researchFreezeMask & VP37_RESEARCH_FREEZE_INTEGRAL_MASK) ==
       0U) &&
      (error < 0.0f) && (self->pid.ki > 0.0f) && isfinite(dt) && (dt > 0.0f)) {
    const float acquired =
        self->pid.terms.integral - self->pid.descentUnwindTarget;
    if (acquired < 0.0f) {
      // Return only the qualified share while approaching the first arrival.
      input =
          -acquired / (self->pid.ki * (VP37_DESCENT_INTEGRAL_UNWIND_S + dt));
      integrationBand = 0.0f;
    }
  }
  self->pid.integralInput = input;
  const float extra = extraD + self->pid.kp * (error - input);
  // Restore ordinary P and include every term before conditional anti-windup.
  hal_pid_controller_set_output_limits(self->pid.controller,
                                       self->pid.negativeLimit - extra,
                                       self->pid.upperLimit - extra);
  const hal_status_t status = hal_pid_controller_step_ex(
      self->pid.controller, input, (float)self->feedback.position, dt,
      integrationBand, &self->pid.terms);
  hal_pid_controller_set_output_limits(
      self->pid.controller, self->pid.negativeLimit, self->pid.upperLimit);
  if (status == HAL_OK) {
    self->pid.terms.proportional = self->pid.kp * error;
    self->pid.terms.derivative += extraD;
    self->pid.terms.unconstrained += extra;
    self->pid.terms.output =
        hal_constrain(self->pid.terms.output + extra, self->pid.negativeLimit,
                      self->pid.upperLimit);
  }
  return status;
}
