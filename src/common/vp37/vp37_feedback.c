// VP37 position feedback: the Adjustometer transfer and the calibration sweep
// that fixes the quantity range the rest of the module works in.

#include "vp37_internal.h"

#include <math.h>

/** @brief What a settled calibration window yields. */
typedef struct {
  int32_t position;     /**< Trimmed mean of the window's positions [Hz]. */
  int32_t fuelTempSumC; /**< Sum of the window's fuel temperatures [C]. */
  bool fuelTempValid;   /**< No reading of the window had a broken sensor. */
} VP37CalibrationWindow;

static bool VP37_waitForCalibrationSettle(VP37Pump *self,
                                          VP37CalibrationWindow *window,
                                          bool probe, uint16_t pwm);
/* The settle window keeps one bit per slot of its fuel-sensor state. */
JB_STATIC_ASSERT(VP37_CALIBRATION_STABLE_SAMPLES <= 32U,
                 "one bit per calibration window slot");
static int32_t VP37_getMaxAdjustometerPWMVal(VP37Pump *self);
static int32_t VP37_divideRounded(int32_t numerator, int32_t denominator);

/**
 * @brief Refresh cached Adjustometer position and telemetry values.
 * @return True when the feedback transfer succeeds.
 * @note The refreshed values are a project-local G149-like quantity feedback
 * plus G81-like fuel temperature and supply-voltage telemetry.
 */
bool VP37_updateAdjustometerPosition(VP37Pump *self) {
  const adjustometer_reading_t reading = VP37_readAdjustometer(self);
  self->feedback.readStatus = reading.readStatus;
  self->feedback.readUs = reading.readUs;
  self->feedback.retries = reading.readRetries;
  if (reading.commOk) {
    // A lost signal or a pending baseline leaves no position to read; the
    // step stops on that status, so the last position stays for telemetry.
    if (VP37_readingHasPosition(&reading)) {
      self->feedback.position = VP37_readingPosition(&reading);
      self->feedback.positionValid = true;
    }
    self->feedback.commLostSince = 0;
    self->feedback.commFailed = false;
    self->feedback.lastStatus = reading.status;
    self->feedback.fresh = reading.feedbackFresh;
    self->feedback.rawHz = reading.rawHz;
    self->feedback.filteredHz = reading.signalHz;
    self->feedback.sampleNumber = reading.sampleNumber;
    self->feedback.sampleUs = reading.measuredUs;
    self->feedback.ageUs = reading.ageUs;

    self->feedback.fuelTempC = reading.fuelTempC;
    self->feedback.supplyVolts = reading.voltageRaw * 0.1f;
    self->feedback.readCount++;
  } else {
    self->feedback.fresh = false;
    if (!self->feedback.commFailed) {
      self->feedback.commFailed = true;
      self->feedback.commLostSince = hal_millis();
    }
  }
  return reading.commOk;
}

/**
 * @brief Run the VP37 calibration sweep and capture Adjustometer limits.
 * @return True when both end positions settle and form a valid range.
 * @note This calibrates the project-local N146/G149-like quantity-feedback
 * range, not an OEM mg/stroke model.
 */
bool VP37_makeCalibration(VP37Pump *self) {
#if VP37_TELEMETRY_ENABLED
  (void)VP37_calibrationTraceBegin();
#endif
  VP37Feedback *const feedback = &self->feedback;
  // Positions are signed, so no value marks an endpoint as missing: until
  // both settle the range is zero and calibrationDone says it is not usable.
  feedback->calibrationDone = false;
  feedback->calibrationFtValid = false;
  feedback->calibrationFtDeciC = 0;
  feedback->fuelTempFilteredC = 0.0f;
  feedback->limitCorrectionMin = 0;
  feedback->limitCorrectionMax = 0;
  feedback->calibratedMin = 0;
  feedback->calibratedMax = 0;
  feedback->adjustMin = 0;
  feedback->adjustMiddle = 0;
  feedback->adjustMax = 0;
  VP37CalibrationWindow minWindow = {0};
  VP37CalibrationWindow maxWindow = {0};

  // Capture the natural/resting endpoint first.  Measuring MIN after a strong
  // MAX pulse biases it with actuator hysteresis and oscillator thermal drift.
  self->callbacks.writeQuantityPwm(0);
  bool minSettled = VP37_waitForCalibrationSettle(self, &minWindow, false, 0U);
  if (minSettled) {
    feedback->calibratedMin = minWindow.position;
  }

  bool maxSettled = false;
  if (minSettled) {
    const uint16_t pwm = (uint16_t)VP37_getMaxAdjustometerPWMVal(self);
    self->callbacks.writeQuantityPwm(pwm);
    maxSettled = VP37_waitForCalibrationSettle(self, &maxWindow, false, pwm);
    if (maxSettled) {
      feedback->calibratedMax = maxWindow.position;
    }
  }
  self->callbacks.writeQuantityPwm(0);
  feedback->adjustMin = feedback->calibratedMin;
  feedback->adjustMax = feedback->calibratedMax;

  if (!maxSettled || !minSettled) {
#if VP37_TELEMETRY_ENABLED
    VP37_calibrationTraceEnd(self, minSettled, maxSettled);
#endif
    derr("VP37 calibration timeout: maxSettled=%d minSettled=%d MAX=%d MIN=%d",
         maxSettled, minSettled, feedback->adjustMax, feedback->adjustMin);
    return false;
  }

  // The reference of the fuel-temperature correction: the mean temperature
  // of the readings that gave both endpoints.
  feedback->calibrationFtValid =
      minWindow.fuelTempValid && maxWindow.fuelTempValid;
  if (feedback->calibrationFtValid) {
    feedback->calibrationFtDeciC = (int16_t)VP37_divideRounded(
        (minWindow.fuelTempSumC + maxWindow.fuelTempSumC) * 10,
        (int32_t)VP37_CALIBRATION_STABLE_SAMPLES * 2);
    // The filter starts where the limits were measured: no correction yet.
    feedback->fuelTempFilteredC = (float)feedback->calibrationFtDeciC * 0.1f;
  }
  feedback->adjustMiddle =
      ((feedback->adjustMax - feedback->adjustMin) / 2) + feedback->adjustMin;
  const int32_t calibrationTravel = feedback->adjustMax - feedback->adjustMin;
  // The signed axis may put the whole stroke on either side of zero, so the
  // travel alone qualifies the range.
  feedback->calibrationDone =
      calibrationTravel >= VP37_CALIBRATION_MIN_TRAVEL_HZ;
#if VP37_TELEMETRY_ENABLED
  VP37_calibrationTraceEnd(self, minSettled, maxSettled);
#endif
  if (!feedback->calibrationDone) {
    derr(
        "VP37 calibration range invalid: MIN=%d MAX=%d travel=%d (required=%d)",
        feedback->adjustMin, feedback->adjustMax, calibrationTravel,
        VP37_CALIBRATION_MIN_TRAVEL_HZ);
  }

  hal_pid_controller_set_output_limits(self->pid.controller,
                                       -VP37_PID_CORR_LIMIT_NEGATIVE,
                                       VP37_PID_CORR_LIMIT_POSITIVE_COLD);
  hal_pid_controller_reset(self->pid.controller);
  deb("VP37 calibration: MIN=%d MIDDLE=%d MAX=%d FT=%d/10 valid=%d",
      feedback->adjustMin, feedback->adjustMiddle, feedback->adjustMax,
      (int)feedback->calibrationFtDeciC, feedback->calibrationFtValid);
  return feedback->calibrationDone;
}

#if VP37_LIMIT_FT_CORRECTION_ENABLED
/**
 * @brief One step of the applied correction toward its target.
 * @param applied Correction in effect [Hz].
 * @param target Correction the present fuel temperature asks for [Hz].
 * @return The correction after this step, at most
 * VP37_LIMIT_FT_RATE_HZ_PER_STEP from @p applied.
 */
static int16_t VP37_limitCorrectionStep(int16_t applied, int32_t target) {
  const int32_t step =
      hal_constrain(target - (int32_t)applied, -VP37_LIMIT_FT_RATE_HZ_PER_STEP,
                    VP37_LIMIT_FT_RATE_HZ_PER_STEP);
  return (int16_t)((int32_t)applied + step);
}
#endif

bool VP37_followFuelTemperature(VP37Pump *self) {
  bool changed = false;
#if VP37_LIMIT_FT_CORRECTION_ENABLED
  VP37Feedback *const feedback = &self->feedback;
  // Without a reference there is nothing to correct from; with the sensor
  // broken now the filter and the correction keep the values they have.
  if (feedback->calibrationDone && feedback->calibrationFtValid &&
      ((feedback->lastStatus & ADJ_STATUS_FUEL_TEMP_BROKEN) == 0U)) {
    // Whole-degree readings toggle at a boundary; the filter averages them.
    const float dt = (float)self->pidDtUs * 0.000001f;
    feedback->fuelTempFilteredC =
        hal_math_low_pass(dt / ((float)VP37_LIMIT_FT_FILTER_TAU_S + dt),
                          feedback->fuelTempC, feedback->fuelTempFilteredC);
    const float maxDeltaC = (float)VP37_LIMIT_FT_MAX_DELTA_C;
    const float deltaC =
        hal_constrain(feedback->fuelTempFilteredC -
                          ((float)feedback->calibrationFtDeciC * 0.1f),
                      -maxDeltaC, maxDeltaC);
    // Slopes in 0.1 Hz/C.
    const int16_t minCorrection = VP37_limitCorrectionStep(
        feedback->limitCorrectionMin,
        (int32_t)lroundf((float)VP37_LIMIT_FT_REST_SLOPE_DHZ_PER_C * 0.1f *
                         deltaC));
    const int16_t maxCorrection = VP37_limitCorrectionStep(
        feedback->limitCorrectionMax,
        (int32_t)lroundf((float)VP37_LIMIT_FT_TOP_SLOPE_DHZ_PER_C * 0.1f *
                         deltaC));
    changed = (minCorrection != feedback->limitCorrectionMin) ||
              (maxCorrection != feedback->limitCorrectionMax);
    if (changed) {
      feedback->limitCorrectionMin = minCorrection;
      feedback->limitCorrectionMax = maxCorrection;
      feedback->adjustMin = feedback->calibratedMin + (int32_t)minCorrection;
      feedback->adjustMax = feedback->calibratedMax + (int32_t)maxCorrection;
      feedback->adjustMiddle =
          ((feedback->adjustMax - feedback->adjustMin) / 2) +
          feedback->adjustMin;
    }
  }
#else
  (void)self;
#endif
  return changed;
}

/**
 * @brief Divide and round half away from zero.
 * @param numerator Any value whose magnitude leaves room for half of
 * @p denominator.
 * @param denominator Positive divisor.
 * @return The quotient rounded to the nearest integer.
 */
static int32_t VP37_divideRounded(int32_t numerator, int32_t denominator) {
  const int32_t half = denominator / 2;
  return (numerator >= 0) ? ((numerator + half) / denominator)
                          : -((half - numerator) / denominator);
}

/**
 * @brief Wait until a calibration endpoint is mechanically stable.
 * @param self VP37 controller instance supplying the board services.
 * @param window Output receiving the trimmed mean position of the stable
 * window and the fuel temperature of its readings; changed only on success.
 * @param probe True for the bench probe, which gives up on the first invalid
 * reading. Both modes retain samples without formatting in this loop.
 * @param pwm Command applied while waiting, for the calibration trace.
 * @return True when a stable valid window is found before timeout.
 */
static bool VP37_waitForCalibrationSettle(VP37Pump *self,
                                          VP37CalibrationWindow *window,
                                          bool probe, uint16_t pwm) {
#if !VP37_TELEMETRY_ENABLED
  (void)pwm;
#endif
  int32_t samples[VP37_CALIBRATION_STABLE_SAMPLES] = {0};
  int16_t fuelTemps[VP37_CALIBRATION_STABLE_SAMPLES] = {0};
  uint32_t fuelTempBroken = 0U; /* Bit per window slot: sensor broken. */
  uint32_t sampleCount = 0U;
  uint32_t nextSample = 0U;
  const uint32_t startMs = hal_millis();

  while (!hal_millis_deadline_expired(startMs, VP37_CALIBRATION_TIMEOUT_MS)) {
    hal_delay_ms(VP37_CALIBRATION_SAMPLE_INTERVAL_MS);
    self->callbacks.feedWatchdog();

    if (probe &&
        (!self->callbacks.driveAllowed() || !self->callbacks.driveEnabled())) {
      return false;
    }
    const adjustometer_reading_t reading = VP37_readAdjustometer(self);
#if VP37_TELEMETRY_ENABLED
    VP37_calibrationTraceRecord(&reading, pwm);
#endif
    if (!reading.commOk || !reading.feedbackFresh ||
        (reading.status &
         (ADJ_STATUS_SIGNAL_LOST | ADJ_STATUS_BASELINE_PENDING)) != 0U) {
      if (probe) {
        return false;
      }
      sampleCount = 0U;
      nextSample = 0U;
      continue;
    }

    samples[nextSample] = VP37_readingPosition(&reading);
    fuelTemps[nextSample] = reading.fuelTempC;
    if ((reading.status & ADJ_STATUS_FUEL_TEMP_BROKEN) != 0U) {
      fuelTempBroken |= 1U << nextSample;
    } else {
      fuelTempBroken &= ~(1U << nextSample);
    }
    nextSample = (nextSample + 1U) % VP37_CALIBRATION_STABLE_SAMPLES;
    if (sampleCount < VP37_CALIBRATION_STABLE_SAMPLES) {
      sampleCount++;
    }

    if (!hal_elapsed_u32(hal_millis(), startMs,
                         VP37_CALIBRATION_MIN_SETTLE_MS) ||
        (sampleCount < VP37_CALIBRATION_STABLE_SAMPLES)) {
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
      const uint32_t kept = VP37_CALIBRATION_STABLE_SAMPLES - 2U;
      window->position = sum / (int32_t)kept;
      window->fuelTempSumC = 0;
      for (uint32_t i = 0U; i < VP37_CALIBRATION_STABLE_SAMPLES; i++) {
        window->fuelTempSumC += (int32_t)fuelTemps[i];
      }
      window->fuelTempValid = fuelTempBroken == 0U;
      return true;
    }
  }

  return false;
}

/**
 * @brief Get the PWM value used during maximum-range calibration.
 * @return PWM command used for calibration.
 */
static int32_t VP37_getMaxAdjustometerPWMVal(VP37Pump *self) {
  (void)self;
  return hal_map(VP37_CALIBRATION_MAX_PERCENTAGE, 0, 100, 0,
                 VP37_PWM_RESOLUTION);
}

#if VP37_TELEMETRY_ENABLED
hal_status_t VP37_probeCalibrationMaximum(VP37Pump *self, int32_t *maximum) {
  if ((self == NULL) || (maximum == NULL)) {
    return HAL_EINVAL;
  }
  if (!VP37_isReady(self) || !self->demand.atRest ||
      !self->demand.targetValid || !self->demand.desiredValid ||
      self->demand.requestedPercent != 0.0f || self->output.finalPWM != 0 ||
      self->demand.target != self->feedback.adjustMin ||
      self->demand.desired != self->feedback.adjustMin ||
      self->pid.terms.integral != 0.0f || !self->callbacks.driveAllowed() ||
      !self->callbacks.driveEnabled()) {
    return HAL_ESTATE;
  }
  const int32_t pwm = VP37_getMaxAdjustometerPWMVal(self);
  VP37CalibrationWindow window = {0};
  const hal_status_t capture = VP37_calibrationTraceBegin();
  if (capture != HAL_OK) {
    return capture;
  }
  self->callbacks.writeQuantityPwm(pwm);
  const bool settled = VP37_waitForCalibrationSettle(
      self, &window, true, (uint16_t)VP37_getMaxAdjustometerPWMVal(self));
  self->callbacks.writeQuantityPwm(0);
  self->controlStarted = false;
  self->pidStarted = false;
  if (settled) {
    *maximum = window.position;
  }
  VP37_calibrationProbeEnd(self, (uint16_t)pwm, settled, window.position);
  return settled ? HAL_OK : HAL_ETIMEOUT;
}
#endif
