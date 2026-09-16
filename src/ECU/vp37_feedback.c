// VP37 position feedback: the Adjustometer transfer and the calibration sweep
// that fixes the quantity range the rest of the module works in.

#include "vp37_internal.h"
#include <utils/multicoreWatchdog.h>

static bool VP37_waitForCalibrationSettle(VP37Pump *self,
                                          int32_t *settledValue);
static int32_t VP37_getMaxAdjustometerPWMVal(VP37Pump *self);

/**
 * @brief Refresh cached Adjustometer position and telemetry values.
 * @param self VP37 controller instance to update.
 * @return True when the feedback transfer succeeds.
 * @note The refreshed values are a project-local G149-like quantity feedback
 * plus G81-like fuel temperature and supply-voltage telemetry.
 */
bool VP37_updateAdjustometerPosition(VP37Pump *self) {
  adjustometer_reading_t reading;
  getVP37Adjustometer(&reading);
  self->feedback.feedbackReadStatus =
      reading.fastFeedback ? reading.readStatus
                           : (reading.commOk ? HAL_OK : HAL_EBUS);
  self->feedback.feedbackReadUs = reading.readUs;
  self->feedback.feedbackRetries = reading.readRetries;
  if (reading.commOk) {
    self->feedback.currentAdjustometerPosition = reading.pulseHz;
    self->feedback.adjCommLostSince = 0;
    self->feedback.adjCommFailed = false;
    self->feedback.lastAdjustometerStatus = reading.status;
    self->feedback.feedbackFresh =
        !reading.fastFeedback || reading.feedbackFresh;
    self->feedback.feedbackRawHz = reading.rawHz;
    self->feedback.feedbackFilteredHz = reading.signalHz;
    self->feedback.feedbackNumber = reading.sampleNumber;
    self->feedback.feedbackUs = reading.measuredUs;
    self->feedback.feedbackAgeUs = reading.ageUs;

    setGlobalValue(F_FUEL_TEMP, reading.fuelTempC);
    setGlobalValue(F_VOLTS, reading.voltageRaw * 0.1f);
  } else {
    self->feedback.feedbackFresh = false;
    if (!self->feedback.adjCommFailed) {
      self->feedback.adjCommFailed = true;
      self->feedback.adjCommLostSince = hal_millis();
    }
  }
  return reading.commOk;
}

/**
 * @brief Run the VP37 calibration sweep and capture Adjustometer limits.
 * @param self VP37 controller instance to calibrate.
 * @return True when both end positions settle and form a valid range.
 * @note This calibrates the project-local N146/G149-like quantity-feedback
 * range, not an OEM mg/stroke model.
 */
bool VP37_makeCalibration(VP37Pump *self) {
  self->feedback.VP37_ADJUST_MAX = self->feedback.VP37_ADJUST_MIDDLE =
      self->feedback.VP37_ADJUST_MIN = self->feedback.VP37_OPERATE_MAX = -1;

  // Capture the natural/resting endpoint first.  Measuring MIN after a strong
  // MAX pulse biases it with actuator hysteresis and oscillator thermal drift.
  valToPWM(PIO_VP37_RPM, 0);
  bool minSettled =
      VP37_waitForCalibrationSettle(self, &self->feedback.VP37_ADJUST_MIN);

  bool maxSettled = false;
  if (minSettled) {
    valToPWM(PIO_VP37_RPM, VP37_getMaxAdjustometerPWMVal(self));
    maxSettled =
        VP37_waitForCalibrationSettle(self, &self->feedback.VP37_ADJUST_MAX);
  }
  valToPWM(PIO_VP37_RPM, 0);

  if (!maxSettled || !minSettled) {
    self->feedback.calibrationDone = false;
    derr("VP37 calibration timeout: maxSettled=%d minSettled=%d MAX=%d MIN=%d",
         maxSettled, minSettled, self->feedback.VP37_ADJUST_MAX,
         self->feedback.VP37_ADJUST_MIN);
    return false;
  }

  self->feedback.VP37_ADJUST_MIDDLE =
      ((self->feedback.VP37_ADJUST_MAX - self->feedback.VP37_ADJUST_MIN) / 2) +
      self->feedback.VP37_ADJUST_MIN;
  const int32_t calibrationTravel =
      self->feedback.VP37_ADJUST_MAX - self->feedback.VP37_ADJUST_MIN;
  self->feedback.calibrationDone =
      calibrationTravel >= VP37_CALIBRATION_MIN_TRAVEL_HZ &&
      self->feedback.VP37_ADJUST_MIDDLE > 0;
  if (!self->feedback.calibrationDone) {
    derr(
        "VP37 calibration range invalid: MIN=%d MAX=%d travel=%d (required=%d)",
        self->feedback.VP37_ADJUST_MIN, self->feedback.VP37_ADJUST_MAX,
        calibrationTravel, VP37_CALIBRATION_MIN_TRAVEL_HZ);
  }

  hal_pid_controller_set_output_limits(self->pid.adjustController,
                                       -VP37_PID_CORR_LIMIT_NEGATIVE,
                                       VP37_PID_CORR_LIMIT_POSITIVE_COLD);
  hal_pid_controller_reset(self->pid.adjustController);
  deb("VP37 calibration: MIN=%d MIDDLE=%d MAX=%d OPERATE_MAX=%d",
      self->feedback.VP37_ADJUST_MIN, self->feedback.VP37_ADJUST_MIDDLE,
      self->feedback.VP37_ADJUST_MAX, self->feedback.VP37_OPERATE_MAX);
  return self->feedback.calibrationDone;
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
        self->feedback.adjustStabilityTable[i] = samples[i];
      }
      return true;
    }
  }

  return false;
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
