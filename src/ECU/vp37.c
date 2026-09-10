#include "vp37.h"
#include <math.h>

#include <hal/timers/hal_soft_timer.h>
#include <utils/multicoreWatchdog.h>

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
  const VP37TraceSample sample = {.us = self->controlLastUs,
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
                                  .fuelTemp = self->lastFuelTemp,
                                  .temperatureCorrection =
                                      self->temperatureCorrection,
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
                                  .pidDtUs = self->pidDtUs};
  return sample;
}

#ifdef START_TEST_ENABLE_VP37_CYCLIC
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
  self->adjCommFailed = false;
  self->pidErr = 0;
  self->pwmFeedForward = VP37_PWM_FF_AT_MIN;
  self->feedForwardRiseBlend = 0.0f;
  self->feedForwardMotion = 0.0f;
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
  self->temperatureReady = false;
#if defined(START_TEST_ENABLE_VP37_CYCLIC) ||                                  \
    defined(START_TEST_ENABLE_VP37_POTENTIOMETER)
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

static float VP37_feedForward(VP37Pump *self, int32_t position) {
  // Positive-demand map at 12 V, 49 C; the upper holding command bends
  // downward.
  static const struct {
    float percent, pwm, motion;
  } points[] = {{0.0f, VP37_PWM_FF_AT_MIN, 0.0f},
                {5.0f, 610.0f, 10.0f},
                {10.0f, 635.0f, 12.0f},
                {25.0f, 705.0f, 15.0f},
                {50.0f, 790.0f, 20.0f},
                {75.0f, 835.0f, 22.0f},
                {90.0f, 835.0f, VP37_PWM_FF_MOTION_BOOST},
                {95.0f, 828.0f, VP37_PWM_FF_MOTION_BOOST},
                {100.0f, VP37_PWM_FF_AT_MAX, VP37_PWM_FF_MOTION_BOOST}};
  const float percent = hal_constrain(
      hal_math_map_f32((float)position, (float)self->VP37_ADJUST_MIN,
                       (float)self->VP37_ADJUST_MAX, 0.0f, 100.0f),
      0.0f, 100.0f);
  for (size_t i = 1U; i < COUNTOF(points); ++i) {
    if (percent <= points[i].percent) {
      const float holding =
          hal_math_map_f32(percent, points[i - 1U].percent, points[i].percent,
                           points[i - 1U].pwm, points[i].pwm);
      self->feedForwardMotion =
          self->feedForwardRiseBlend *
          hal_math_map_f32(percent, points[i - 1U].percent, points[i].percent,
                           points[i - 1U].motion, points[i].motion);
      return holding + self->feedForwardMotion;
    }
  }
  self->feedForwardMotion = 0.0f;
  return VP37_PWM_FF_AT_MAX;
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

  const float trimStart = hal_math_map_f32(VP37_PID_TRIM_TAPER_PERCENT, 0.0f,
                                           100.0f, (float)self->VP37_ADJUST_MIN,
                                           (float)self->VP37_ADJUST_MAX);
  const float trimLimit =
      hal_constrain(hal_math_map_f32(self->desiredPosition, trimStart,
                                     (float)self->VP37_ADJUST_MAX,
                                     VP37_PID_TRIM_PWM, VP37_PID_TRIM_TOP_PWM),
                    VP37_PID_TRIM_TOP_PWM, VP37_PID_TRIM_PWM);
  self->pidIntegralLimit = trimLimit;
  if (self->pidIntegralOverride > 0.0f) {
    self->pidIntegralLimit =
        fminf(self->pidIntegralLimit, self->pidIntegralOverride);
  }
  const float ki = hal_pid_controller_get_ki(self->adjustController);
  const float maxIntegral = ki > 0.0f ? self->pidIntegralLimit / ki : 0.0f;
  hal_pid_controller_set_max_integral(self->adjustController, maxIntegral);
  self->lastVolts = getGlobalValue(F_VOLTS);
  if (self->lastVolts < VP37_MIN_COMPENSATION_VOLTAGE) {
    self->lastVolts = VP37_MIN_COMPENSATION_VOLTAGE;
  }
  self->lastFuelTemp = getGlobalValue(F_FUEL_TEMP);
  self->voltageCorrection = NOMINAL_VOLTAGE / self->lastVolts;
  VP37_updateTemperatureCorrection(self, dt);
  const float outputScale =
      self->voltageCorrection * self->temperatureCorrection;

  // Finish the commanded descent before releasing the spring-return actuator.
  // Neither feedforward nor a retained integral may energize it at zero demand.
  self->quantityAtRest = self->lastThrottle <= (float)VP37_PERCENT_MIN &&
                         self->desiredPosition <= (float)self->VP37_ADJUST_MIN;
  if (self->quantityAtRest) {
    hal_pid_controller_reset(self->adjustController);
    self->pidTerms = (hal_pid_terms_t){0};
    self->pwmFeedForward = 0.0f;
    self->feedForwardRiseBlend = 0.0f;
    self->feedForwardMotion = 0.0f;
    self->pidCorrection = 0.0f;
    self->pwmValue = 0.0f;
    self->pidNegativeLimit = 0.0f;
    self->pidUpperLimit = 0.0f;
    self->pidSaturatedHigh = false;
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
  const float integralDeadband =
      rampWindup ? fabsf((float)self->pidErr) : (float)VP37_PID_DEADBAND;
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
#ifdef START_TEST_ENABLE_VP37_CYCLIC
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
      "age:%u io:%d ious:%lu retry:%u fresh:%d pdt:%lu V:%.1f ft:%.0f tcf:%.4f "
      "mff:%.1f",
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
      (unsigned long)sample->pidDtUs, sample->volts, sample->fuelTemp,
      sample->temperatureCorrection, sample->motionFF);
}

#ifdef START_TEST_ENABLE_VP37_CYCLIC
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
#ifdef START_TEST_ENABLE_VP37_CYCLIC
    const char *mode = "cyclic";
    const unsigned int cycleDelayMs = CYCLIC_DELAYTIME;
#elif defined(START_TEST_ENABLE_VP37_POTENTIOMETER)
    const char *mode = "potentiometer";
    const unsigned int cycleDelayMs = 0U;
#else
    const char *mode = "engine";
    const unsigned int cycleDelayMs = 0U;
#endif
    deb("VP37 CFG rev:28 kp:%.4f ki:%.4f kd:%.5f tf:%.4f tu:%.1f "
        "min:%d max:%d V:%.1f t:%.1fC imax:%.1f tw:%.2f tcf:%.4f mode:%s "
        "cyclic_ms:%u slew:%.1f upper_slew:%.1f",
        self->pidKp, self->pidKi, self->pidKd, self->pidTf, self->pidTimeUpdate,
        self->VP37_ADJUST_MIN, self->VP37_ADJUST_MAX, self->lastVolts,
        self->lastFuelTemp, self->pidIntegralLimit,
        self->temperatureCompensationWeight, self->temperatureCorrection, mode,
        cycleDelayMs, VP37_DESIRED_SLEW_PERCENT_PER_SECOND,
        VP37_DESIRED_UPPER_SLEW_PERCENT_PER_SECOND);
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
