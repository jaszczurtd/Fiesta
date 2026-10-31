// VP37 tuning: the gains, limits and switches a bench console may change, each
// checked against its range here, so no caller writes the pump directly.

#include "vp37_internal.h"

#include <float.h>
#include <math.h>

/** @brief Accepted values of one parameter; the lower bound is inclusive. */
typedef struct {
  float min;
  float max;
  bool whole; /**< Integers only. */
} VP37TuningRange;

/** @brief Whether @p value is finite and inside the range of @p param. */
static bool VP37_tuningAccepts(VP37TuningParam param, float value) {
  static const VP37TuningRange k_ranges[VP37_TUNING_COUNT] = {
      [VP37_TUNING_RESEARCH_DESCENT_SHARE] = {0.25f, 0.75f, false},
      [VP37_TUNING_RESEARCH_DESCENT_UNWIND] = {0.0f, 1.0f, true},
      [VP37_TUNING_RESEARCH_RAMP_BRAKING] = {0.0f, 1.0f, true},
      [VP37_TUNING_RESEARCH_UPPER_DEADBAND] = {0.0f, 1.0f, true},
      [VP37_TUNING_RESEARCH_RAMP_DAMPING] = {0.0f, 1.0f, true},
      [VP37_TUNING_RESEARCH_REFERENCE_WEIGHT] = {0.0f, 1.0f, false},
      [VP37_TUNING_RESEARCH_SLOW_DAMPING] = {0.0f, 1.0f, true},
      [VP37_TUNING_RESEARCH_MOTION_INTEGRAL] = {0.0f, 1.0f, true},
      [VP37_TUNING_RESEARCH_FREEZE] = {0.0f, 3.0f, true},
      [VP37_TUNING_KP] = {0.0f, FLT_MAX, false},
      [VP37_TUNING_KI] = {0.0f, FLT_MAX, false},
      [VP37_TUNING_KD] = {0.0f, FLT_MAX, false},
      [VP37_TUNING_TOP_KD] = {0.0f, VP37_PID_TOP_KD_MAX, false},
      [VP37_TUNING_PERIOD_MS] = {VP37_PID_TIME_UPDATE_MIN,
                                 VP37_PID_TIME_UPDATE_MAX, false},
      [VP37_TUNING_DERIVATIVE_FILTER] = {0.0f, FLT_MAX, false},
      [VP37_TUNING_INTEGRAL_CAP] = {0.0f, VP37_BENCH_INTEGRAL_LIMIT_MAX, false},
      [VP37_TUNING_TEMPERATURE_WEIGHT] = {0.0f, 1.0f, false},
      [VP37_TUNING_HOLD_CONFIRM_MS] = {0.0f,
                                       (float)VP37_INTEGRAL_HOLD_CONFIRM_MAX_MS,
                                       true},
      [VP37_TUNING_DEADBAND_TOP_HZ] = {0.0f, VP37_PID_DEADBAND_TOP_MAX_HZ,
                                       false},
      [VP37_TUNING_MOTION_BOOST_UP] = {0.0f, VP37_PWM_FF_MOTION_BOOST_MAX,
                                       false},
      [VP37_TUNING_MOTION_BOOST_DOWN] = {0.0f, VP37_PWM_FF_MOTION_BOOST_MAX,
                                         false},
      [VP37_TUNING_TOP_ARRIVAL_DECEL] =
          {0.0f, VP37_TOP_ARRIVAL_DECEL_MAX_PERCENT_PER_S2, false},
      [VP37_TUNING_MOTION_RATE_CAP] =
          {0.0f, VP37_PWM_FF_MOTION_RATE_CAP_MAX_PERCENT_PER_S, false},
      [VP37_TUNING_CURRENT_OBSERVATION] = {0.0f, 1.0f, true},
      [VP37_TUNING_CURRENT_FEEDBACK] = {0.0f, 1.0f, true},
      [VP37_TUNING_MAP_TRIM] = {0.0f, 1.0f, true},
      [VP37_TUNING_DRIVE_COMPENSATION] = {0.0f, 1.0f, true},
      [VP37_TUNING_CYCLE_VOLTAGE] = {0.0f, 1.0f, true},
      [VP37_TUNING_SUPPLY_FROZEN] = {0.0f, 1.0f, true},
      [VP37_TUNING_HOLD_OFFSET_MS] = {0.0f,
                                      (float)VP37_INTEGRAL_HOLD_OFFSET_MAX_MS,
                                      true},
      [VP37_TUNING_THERMAL_HOLD] = {0.0f, 1.0f, true},
      [VP37_TUNING_HOLD_CURRENT] = {0.0f, 1.0f, true},
      [VP37_TUNING_HOLD_MEAN_ENTER_HZ] = {VP37_INTEGRAL_HOLD_MEAN_ENTER_MIN_HZ,
                                          (float)VP37_INTEGRAL_HOLD_ENTER_HZ,
                                          false},
  };
  bool accepted = false;
  if (((uint32_t)param < (uint32_t)VP37_TUNING_COUNT) && isfinite(value)) {
    const VP37TuningRange *const range = &k_ranges[param];
    accepted = (value >= range->min) && (value <= range->max) &&
               (!range->whole || (value == floorf(value)));
  }
  return accepted;
}

static void VP37_clearMapTrim(VP37Pump *self) {
  for (uint32_t i = 0U; i < COUNTOF(self->feedforward.mapTrim); i++) {
    self->feedforward.mapTrim[i] = 0.0f;
  }
  self->feedforward.mapTrimTransfers = 0U;
}

static void VP37_setDerivativeFilter(VP37Pump *self, float tf) {
  self->pid.tf = tf;
  hal_pid_controller_set_tf(self->pid.controller, tf);
}

/** @brief Write one accepted parameter and what depends on it. */
static void VP37_applyTuning(VP37Pump *self, VP37TuningParam param,
                             float value) {
  const bool on = value > 0.5f;
  switch (param) {
  case VP37_TUNING_RESEARCH_DESCENT_SHARE:
    self->pid.descentIntegralRetainedShare = value;
    VP37_resetMotionIntegralGate(self);
    break;
  case VP37_TUNING_RESEARCH_DESCENT_UNWIND:
    self->pid.researchDescentIntegralUnwind = on;
    VP37_resetMotionIntegralGate(self);
    break;
  case VP37_TUNING_RESEARCH_RAMP_BRAKING:
    self->pid.researchRampBraking = on;
    break;
  case VP37_TUNING_RESEARCH_UPPER_DEADBAND:
    self->pid.researchUpperDeadband = on;
    break;
  case VP37_TUNING_RESEARCH_RAMP_DAMPING:
    self->pid.researchRampDamping = on;
    self->pid.topDBlend = 0.0f;
    break;
  case VP37_TUNING_RESEARCH_REFERENCE_WEIGHT:
    self->pid.researchReferenceWeight = value;
    VP37_resetSlowDamping(self);
    break;
  case VP37_TUNING_RESEARCH_SLOW_DAMPING:
    self->pid.researchSlowDamping = on;
    VP37_resetSlowDamping(self);
    break;
  case VP37_TUNING_RESEARCH_MOTION_INTEGRAL:
    self->pid.researchMotionIntegralGate = on;
    VP37_resetMotionIntegralGate(self);
    break;
  case VP37_TUNING_RESEARCH_FREEZE:
    self->pid.researchFreezeMask = (uint8_t)value;
    break;
  case VP37_TUNING_KP:
    VP37_setVP37PID(self, value, self->pid.ki, self->pid.kd, false);
    break;
  case VP37_TUNING_KI:
    VP37_setVP37PID(self, self->pid.kp, value, self->pid.kd, false);
    break;
  case VP37_TUNING_KD:
    VP37_setVP37PID(self, self->pid.kp, self->pid.ki, value, false);
    break;
  case VP37_TUNING_TOP_KD:
    self->pid.topKd = value;
    break;
  case VP37_TUNING_PERIOD_MS:
    self->pidTimeUpdate = value;
    break;
  case VP37_TUNING_DERIVATIVE_FILTER:
    VP37_setDerivativeFilter(self, value);
    break;
  case VP37_TUNING_INTEGRAL_CAP:
    self->pid.integralOverride = value;
    break;
  case VP37_TUNING_TEMPERATURE_WEIGHT:
    self->thermal.temperatureCompensationWeight = value;
    break;
  case VP37_TUNING_HOLD_CONFIRM_MS:
    self->pid.integralHoldConfirmMs = (uint32_t)value;
    self->pid.integralHoldEnterPending = false;
    break;
  case VP37_TUNING_DEADBAND_TOP_HZ:
    self->pid.integralDeadbandTopHz = value;
    break;
  case VP37_TUNING_MOTION_BOOST_UP:
    self->feedforward.motionBoostUp = value;
    break;
  case VP37_TUNING_MOTION_BOOST_DOWN:
    self->feedforward.motionBoostDown = value;
    break;
  case VP37_TUNING_TOP_ARRIVAL_DECEL:
    self->demand.topArrivalDecel = value;
    break;
  case VP37_TUNING_MOTION_RATE_CAP:
    self->feedforward.motionRateCap = value;
    break;
  case VP37_TUNING_CURRENT_OBSERVATION:
    self->thermal.observationEnabled = on;
    break;
  case VP37_TUNING_CURRENT_FEEDBACK:
    self->currentControl.enabled = on;
    break;
  case VP37_TUNING_MAP_TRIM:
    self->feedforward.mapTrimEnabled = on;
    VP37_clearMapTrim(self);
    break;
  case VP37_TUNING_DRIVE_COMPENSATION:
    self->thermal.driveCompensationEnabled = on;
    if (!on) {
      self->thermal.driveSamples = 0U;
      self->thermal.driveResistanceReady = false;
      self->thermal.driveCompensationUsed = false;
      self->thermal.driveCorrection = 1.0f;
    }
    break;
  case VP37_TUNING_CYCLE_VOLTAGE:
    self->supply.cycleEnabled = on;
    break;
  case VP37_TUNING_SUPPLY_FROZEN:
    self->supply.frozen = on;
    break;
  case VP37_TUNING_HOLD_OFFSET_MS:
    self->pid.integralHoldOffsetMs = (uint32_t)value;
    self->pid.integralHoldOffsetPending = false;
    break;
  case VP37_TUNING_THERMAL_HOLD:
    self->thermal.scaleHoldEnabled = on;
    break;
  case VP37_TUNING_HOLD_CURRENT:
    self->thermal.scaleHoldCurrent = on;
    break;
  case VP37_TUNING_HOLD_MEAN_ENTER_HZ:
    self->pid.integralHoldMeanEnterHz = value;
    break;
  default:
    /* Unreachable: the range lookup rejected every other value. */
    break;
  }
}

hal_status_t VP37_setTuning(VP37Pump *self, VP37TuningParam param,
                            float value) {
  hal_status_t status = HAL_EINVAL;
  if ((self != NULL) && VP37_tuningAccepts(param, value)) {
    if ((param == VP37_TUNING_RESEARCH_REFERENCE_WEIGHT) &&
        (!self->demand.atRest || self->demand.requestedPercent != 0.0f ||
         self->output.finalPWM != 0 ||
         ((value < 1.0f) &&
          (!self->pid.fullRangePrototype || !(self->pid.ki > 0.0f) ||
           ((self->pid.researchFreezeMask &
             VP37_RESEARCH_FREEZE_INTEGRAL_MASK) != 0U))))) {
      return HAL_ESTATE;
    }
    if ((self->pid.researchReferenceWeight < 1.0f) &&
        (((param == VP37_TUNING_KI) && (value == 0.0f)) ||
         ((param == VP37_TUNING_RESEARCH_FREEZE) &&
          (((uint8_t)value & VP37_RESEARCH_FREEZE_INTEGRAL_MASK) != 0U)))) {
      return HAL_ESTATE;
    }
    if (((param == VP37_TUNING_RESEARCH_SLOW_DAMPING) ||
         (param == VP37_TUNING_RESEARCH_RAMP_DAMPING) ||
         (param == VP37_TUNING_RESEARCH_UPPER_DEADBAND) ||
         (param == VP37_TUNING_RESEARCH_RAMP_BRAKING) ||
         (param == VP37_TUNING_RESEARCH_DESCENT_UNWIND) ||
         (param == VP37_TUNING_RESEARCH_DESCENT_SHARE)) &&
        ((!self->pid.fullRangePrototype && (value != 0.0f)) ||
         !self->demand.atRest || (self->demand.requestedPercent != 0.0f) ||
         (self->output.finalPWM != 0))) {
      return HAL_ESTATE;
    }
    if ((param == VP37_TUNING_RESEARCH_MOTION_INTEGRAL) && (value != 0.0f) &&
        !self->pid.fullRangePrototype) {
      return HAL_ESTATE;
    }
    if ((param == VP37_TUNING_RESEARCH_FREEZE) && (value != 0.0f)) {
      const uint8_t mask = (uint8_t)value;
      if (((mask & VP37_RESEARCH_FREEZE_DRIVE_MASK) != 0U) &&
          (!self->thermal.driveCompensationEnabled ||
           !self->thermal.driveCompensationUsed ||
           !self->thermal.driveResistanceReady || !self->thermal.scaleReady)) {
        return HAL_ESTATE;
      }
    }
    if (self->pid.fullRangePrototype && (value != 0.0f) &&
        ((param == VP37_TUNING_CURRENT_FEEDBACK) ||
         (param == VP37_TUNING_MAP_TRIM) ||
         (param == VP37_TUNING_THERMAL_HOLD))) {
      return HAL_ESTATE;
    }
    if (self->pid.septemberReference && (value != 0.0f) &&
        ((param == VP37_TUNING_CURRENT_FEEDBACK) ||
         (param == VP37_TUNING_DRIVE_COMPENSATION) ||
         (param == VP37_TUNING_CYCLE_VOLTAGE) ||
         (param == VP37_TUNING_MAP_TRIM) ||
         (param == VP37_TUNING_THERMAL_HOLD))) {
      return HAL_ESTATE;
    }
    VP37_applyTuning(self, param, value);
    VP37_resetUpperHolding(self);
    self->pid.holdingIntegralReady = false;
    self->pid.holdingIntegralElapsed = 0.0f;
    status = HAL_OK;
  }
  return status;
}

static float VP37_switchValue(bool on) { return on ? 1.0f : 0.0f; }

/** @brief The stored value of one parameter; switches as 0 or 1. */
static float VP37_tuningValue(const VP37Pump *self, VP37TuningParam param) {
  float value = 0.0f;
  switch (param) {
  case VP37_TUNING_RESEARCH_DESCENT_SHARE:
    value = self->pid.descentIntegralRetainedShare;
    break;
  case VP37_TUNING_RESEARCH_DESCENT_UNWIND:
    value = VP37_switchValue(self->pid.researchDescentIntegralUnwind);
    break;
  case VP37_TUNING_RESEARCH_RAMP_BRAKING:
    value = VP37_switchValue(self->pid.researchRampBraking);
    break;
  case VP37_TUNING_RESEARCH_UPPER_DEADBAND:
    value = VP37_switchValue(self->pid.researchUpperDeadband);
    break;
  case VP37_TUNING_RESEARCH_RAMP_DAMPING:
    value = VP37_switchValue(self->pid.researchRampDamping);
    break;
  case VP37_TUNING_RESEARCH_REFERENCE_WEIGHT:
    value = self->pid.researchReferenceWeight;
    break;
  case VP37_TUNING_RESEARCH_SLOW_DAMPING:
    value = VP37_switchValue(self->pid.researchSlowDamping);
    break;
  case VP37_TUNING_RESEARCH_MOTION_INTEGRAL:
    value = self->pid.researchMotionIntegralGate ? 1.0f : 0.0f;
    break;
  case VP37_TUNING_RESEARCH_FREEZE:
    value = (float)self->pid.researchFreezeMask;
    break;
  case VP37_TUNING_KP:
    value = self->pid.kp;
    break;
  case VP37_TUNING_KI:
    value = self->pid.ki;
    break;
  case VP37_TUNING_KD:
    value = self->pid.kd;
    break;
  case VP37_TUNING_TOP_KD:
    value = self->pid.topKd;
    break;
  case VP37_TUNING_PERIOD_MS:
    value = self->pidTimeUpdate;
    break;
  case VP37_TUNING_DERIVATIVE_FILTER:
    value = self->pid.tf;
    break;
  case VP37_TUNING_INTEGRAL_CAP:
    value = self->pid.integralOverride;
    break;
  case VP37_TUNING_TEMPERATURE_WEIGHT:
    value = self->thermal.temperatureCompensationWeight;
    break;
  case VP37_TUNING_HOLD_CONFIRM_MS:
    value = (float)self->pid.integralHoldConfirmMs;
    break;
  case VP37_TUNING_DEADBAND_TOP_HZ:
    value = self->pid.integralDeadbandTopHz;
    break;
  case VP37_TUNING_MOTION_BOOST_UP:
    value = self->feedforward.motionBoostUp;
    break;
  case VP37_TUNING_MOTION_BOOST_DOWN:
    value = self->feedforward.motionBoostDown;
    break;
  case VP37_TUNING_TOP_ARRIVAL_DECEL:
    value = self->demand.topArrivalDecel;
    break;
  case VP37_TUNING_MOTION_RATE_CAP:
    value = self->feedforward.motionRateCap;
    break;
  case VP37_TUNING_CURRENT_OBSERVATION:
    value = VP37_switchValue(self->thermal.observationEnabled);
    break;
  case VP37_TUNING_CURRENT_FEEDBACK:
    value = VP37_switchValue(self->currentControl.enabled);
    break;
  case VP37_TUNING_MAP_TRIM:
    value = VP37_switchValue(self->feedforward.mapTrimEnabled);
    break;
  case VP37_TUNING_DRIVE_COMPENSATION:
    value = VP37_switchValue(self->thermal.driveCompensationEnabled);
    break;
  case VP37_TUNING_CYCLE_VOLTAGE:
    value = VP37_switchValue(self->supply.cycleEnabled);
    break;
  case VP37_TUNING_SUPPLY_FROZEN:
    value = VP37_switchValue(self->supply.frozen);
    break;
  case VP37_TUNING_HOLD_OFFSET_MS:
    value = (float)self->pid.integralHoldOffsetMs;
    break;
  case VP37_TUNING_THERMAL_HOLD:
    value = VP37_switchValue(self->thermal.scaleHoldEnabled);
    break;
  case VP37_TUNING_HOLD_CURRENT:
    value = VP37_switchValue(self->thermal.scaleHoldCurrent);
    break;
  case VP37_TUNING_HOLD_MEAN_ENTER_HZ:
    value = self->pid.integralHoldMeanEnterHz;
    break;
  default:
    break;
  }
  return value;
}

float VP37_getTuning(const VP37Pump *self, VP37TuningParam param) {
  return (self != NULL) ? VP37_tuningValue(self, param) : 0.0f;
}

void VP37_resetTuning(VP37Pump *self) {
  self->pid.researchFreezeMask = 0U;
  self->pid.researchMotionIntegralGate = false;
  self->pid.researchSlowDamping = false;
  self->pid.researchRampDamping = false;
  self->pid.researchUpperDeadband = false;
  self->pid.researchRampBraking = false;
  self->pid.researchDescentIntegralUnwind = false;
  self->pid.descentIntegralRetainedShare = VP37_DESCENT_INTEGRAL_RETAINED_SHARE;
  self->pid.researchReferenceWeight = 1.0f;
  VP37_resetMotionIntegralGate(self);
  VP37_resetSlowDamping(self);
  self->pid.fullRangePrototype = false;
  VP37_resetUpperHolding(self);
  self->pid.septemberReference = false;
  VP37_setVP37PID(self, VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, true);
  self->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  VP37_setDerivativeFilter(self, VP37_PID_TF);
  self->pid.topKd = VP37_PID_TOP_KD;
  self->pid.integralOverride = VP37_BENCH_INTEGRAL_CAP_PWM;
  self->thermal.temperatureCompensationWeight = 1.0f;
  self->currentControl.enabled = true;
  self->pid.integralHoldConfirmMs = VP37_INTEGRAL_HOLD_CONFIRM_MS;
  self->pid.integralHoldOffsetMs = VP37_INTEGRAL_HOLD_OFFSET_MS;
  self->thermal.scaleHoldEnabled = VP37_THERMAL_HOLD_DEFAULT;
  self->thermal.scaleHoldCurrent = VP37_THERMAL_HOLD_CURRENT_DEFAULT;
  self->pid.integralHoldMeanEnterHz = VP37_INTEGRAL_HOLD_MEAN_ENTER_HZ;
  self->pid.integralDeadbandTopHz = VP37_PID_DEADBAND_TOP_HZ;
  self->feedforward.motionBoostUp = VP37_PWM_FF_MOTION_BOOST_DEFAULT;
  self->feedforward.motionBoostDown = VP37_PWM_FF_DESCENT_BOOST;
  VP37_clearMapTrim(self);
  self->demand.topArrivalDecel = VP37_TOP_ARRIVAL_DECEL_PERCENT_PER_S2;
  self->feedforward.motionRateCap = VP37_PWM_FF_MOTION_RATE_CAP_PERCENT_PER_S;
}

void VP37_setVP37PID(VP37Pump *self, float kp, float ki, float kd,
                     bool shouldTriggerReset) {
  self->pid.kp = kp;
  self->pid.ki = ki;
  self->pid.kd = kd;
  self->pid.effectiveKd = kd;
  hal_pid_controller_set_kp(self->pid.controller, kp);
  hal_pid_controller_set_ki(self->pid.controller, ki);
  hal_pid_controller_set_kd(self->pid.controller, kd);
  VP37_resetUpperHolding(self);

  if (shouldTriggerReset) {
    VP37_resetMotionIntegralGate(self);
    VP37_resetSlowDamping(self);
    VP37_resetCurrentControl(self);
    hal_pid_controller_reset(self->pid.controller);
    self->pid.topDBlend = 0.0f;
    self->pid.integralHold = false;
    self->pid.integralHoldEnterPending = false;
    self->pid.integralHoldReleasePending = false;
    self->pid.integralHoldReleaseStartedMs = 0U;
    self->pid.integralHoldOffsetPending = false;
    self->output.lastPWMval = -1;
    self->output.finalPWM = VP37_PWM_MIN;
  }
}

void VP37_getVP37PIDValues(const VP37Pump *self, float *kp, float *ki,
                           float *kd) {
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

float VP37_getVP37PIDTimeUpdate(const VP37Pump *self) {
  return self->pidTimeUpdate;
}
