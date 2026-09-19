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

void VP37_setVP37PID(VP37Pump *self, float kp, float ki, float kd,
                     bool shouldTriggerReset) {
  self->pid.kp = kp;
  self->pid.ki = ki;
  self->pid.kd = kd;
  self->pid.effectiveKd = kd;
  hal_pid_controller_set_kp(self->pid.controller, kp);
  hal_pid_controller_set_ki(self->pid.controller, ki);
  hal_pid_controller_set_kd(self->pid.controller, kd);

  if (shouldTriggerReset) {
    hal_pid_controller_reset(self->pid.controller);
    self->pid.topDBlend = 0.0f;
    self->pid.integralHold = false;
    self->pid.integralHoldEnterPending = false;
    self->pid.integralHoldReleasePending = false;
    self->pid.integralHoldReleaseStartedMs = 0U;
    self->output.lastPWMval = -1;
    self->output.finalPWM = VP37_PWM_MIN;
  }
}

void VP37_getVP37PIDValues(VP37Pump *self, float *kp, float *ki, float *kd) {
  if (kp != NULL) {
    *kp = self->pid.kp;
  }
  if (ki != NULL) {
    *ki = self->pid.ki;
  }
  if (kd != NULL) {
    *kd = self->pid.kd;
  }
}

float VP37_getVP37PIDTimeUpdate(VP37Pump *self) { return self->pidTimeUpdate; }

float VP37_feedForward(VP37Pump *self, int32_t position) {
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
      self->feedforward.motion =
          (self->feedforward.riseBlend *
           hal_math_map_f32(
               percent, lower[VP37_FF_COL_PERCENT], upper[VP37_FF_COL_PERCENT],
               lower[VP37_FF_COL_MOTION], upper[VP37_FF_COL_MOTION]) *
           (self->feedforward.motionBoostUp / VP37_PWM_FF_MOTION_BOOST)) -
          (self->feedforward.fallBlend * self->feedforward.motionBoostDown);
      self->feedforward.mapTrimApplied = VP37_mapTrimAt(self, percent);
      return (holding * VP37_PWM_FF_HARDWARE_GAIN) +
             self->feedforward.mapTrimApplied + self->feedforward.motion;
    }
  }
  self->feedforward.motion = 0.0f;
  self->feedforward.mapTrimApplied = VP37_mapTrimAt(self, 100.0f);
  return (VP37_PWM_FF_AT_MAX * VP37_PWM_FF_HARDWARE_GAIN) +
         self->feedforward.mapTrimApplied;
}

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

/**
 * @brief Integral authority for the demanded position.
 * @return Authority in nominal PWM counts.
 * @note The position profile tapers the authority toward the top of the
 * stroke; the bench cap only ever lowers what the profile allows.
 */
float VP37_integralLimit(const VP37Pump *self) {
  float limit = VP37_strokeTaper(
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
float VP37_integralDeadband(const VP37Pump *self) {
  const float base = VP37_INTEGRAL_DEADBAND_MAP[0U][VP37_TAPER_COL_VALUE];
  float deadband = base;
  if (self->pid.integralDeadbandTopHz > base) {
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
  if ((self->pid.topKd > 0.0f) && !self->demand.atRest && (percent > 85.0f)) {
    const float dt = (float)self->pidDtUs * 0.000001f;
    const float alpha = dt / (VP37_PID_TOP_D_BLEND_S + dt);
    self->pid.topDBlend = hal_math_low_pass(alpha, targetSettled ? 1.0f : 0.0f,
                                            self->pid.topDBlend);
    weight = hal_constrain(hal_math_map_f32(percent, 85.0f, 90.0f, 0.0f, 1.0f),
                           0.0f, 1.0f);
  } else {
    self->pid.topDBlend = 0.0f;
  }
  self->pid.effectiveKd =
      self->pid.kd + (self->pid.topKd * weight * self->pid.topDBlend);
  hal_pid_controller_set_kd(self->pid.controller, self->pid.effectiveKd);
}

/** @brief Update the settled-target hysteresis that freezes integration. */
void VP37_updateIntegralHold(VP37Pump *self, bool targetSettled) {
  const float absoluteError = fabsf((float)self->pid.error);
  // Fixed bands on purpose: widening them with the dead zone froze the
  // integral up to twice the dead zone from the target and left standing
  // errors of 120 Hz on the upper stroke. There the dead zone alone bounds
  // the error; the hold matters where the band is narrower than the zone.
  const float enterHz = (float)VP37_INTEGRAL_HOLD_ENTER_HZ;
  const float exitHz = (float)VP37_INTEGRAL_HOLD_EXIT_HZ;
  self->pid.integralHoldEntered = false;
  if (!targetSettled) {
    self->pid.integralHold = false;
    self->pid.integralHoldEnterPending = false;
    self->pid.integralHoldReleasePending = false;
  } else if (!self->pid.integralHold) {
    if (absoluteError > enterHz) {
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
      }
    }
  } else if (absoluteError <= exitHz) {
    self->pid.integralHoldReleasePending = false;
  } else if (!self->pid.integralHoldReleasePending) {
    self->pid.integralHoldReleasePending = true;
    self->pid.integralHoldReleaseStartedMs = hal_millis();
  } else {
    if (hal_millis_deadline_expired(self->pid.integralHoldReleaseStartedMs,
                                    VP37_INTEGRAL_HOLD_RELEASE_MS)) {
      self->pid.integralHold = false;
      self->pid.integralHoldEnterPending = false;
      self->pid.integralHoldReleasePending = false;
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
      (lower + 1U < VP37_MAP_TRIM_KNOTS) ? (lower + 1U) : lower;
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

/** @brief Learned holding-map residual, interpolated between knots. */
static float VP37_mapTrimAt(const VP37Pump *self, float percent) {
  if (!self->feedforward.mapTrimEnabled) {
    return 0.0f;
  }
  uint32_t lower = 0U;
  float weight = 0.0f;
  VP37_mapTrimKnots(percent, &lower, &weight);
  const uint32_t upper =
      (lower + 1U < VP37_MAP_TRIM_KNOTS) ? (lower + 1U) : lower;
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
