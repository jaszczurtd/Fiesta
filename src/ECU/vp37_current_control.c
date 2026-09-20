// Current feedback shares the position command's reference domain. The source
// shunt observes only ON current, so this does not estimate full-coil RMS.
#include "vp37_internal.h"
#include <math.h>
#include <string.h>

static float VP37_currentCountsPerAmp(float voltageScale) {
  // R is learned from the raw divider, while output uses its calibrated
  // voltage. Keep that reference convention when translating the map to amps.
  return ((float)PWM_RESOLUTION * VP37_DRIVE_REFERENCE_OHMS * voltageScale) /
         NOMINAL_VOLTAGE;
}

void VP37_resetCurrentControl(VP37Pump *self) {
  const bool enabled = self->currentControl.enabled;
  (void)memset(&self->currentControl, 0, sizeof(self->currentControl));
  self->currentControl.enabled = enabled;
}

void VP37_recordCurrentCommand(VP37Pump *self, float nominalPWM,
                               int32_t deliveredPWM, uint32_t writtenUs) {
  VP37CurrentControl *control = &self->currentControl;
  const float scale = self->supply.localScale;
  if (isfinite(nominalPWM) && (nominalPWM > 0.0f) && isfinite(scale) &&
      (scale >= VP37_LOCAL_VOLTAGE_SCALE_MIN) &&
      (scale <= VP37_LOCAL_VOLTAGE_SCALE_MAX)) {
    VP37CurrentCommand *entry = &control->history[control->next];
    entry->writtenUs = writtenUs;
    entry->nominalPwm = nominalPWM;
    entry->voltageScale = scale;
    entry->pwm = deliveredPWM;
    control->next = (control->next + 1U) % COUNTOF(control->history);
    if (control->count < COUNTOF(control->history)) {
      control->count++;
    }
    control->targetAmps = nominalPWM / VP37_currentCountsPerAmp(scale);
  } else {
    VP37_resetCurrentControl(self);
  }
}

static const VP37CurrentCommand *
VP37_findCurrentCommand(const VP37CurrentControl *control,
                        const VP37CurrentPulseResult *sample, uint32_t now) {
  const VP37CurrentCommand *matched = NULL;
  // The inverted drive conducts late in the PWM period. Its preceding
  // current falling edge is the wrap that latched this period's compare.
  const uint32_t latchAge = now - sample->latchUs;
  uint32_t bestAge = UINT32_MAX;
  bool ambiguous = false;
  for (uint32_t i = 0U; i < control->count; i++) {
    const VP37CurrentCommand *entry = &control->history[i];
    const uint32_t age = now - entry->writtenUs;
    const uint32_t distance =
        (age >= latchAge) ? (age - latchAge) : (latchAge - age);
    if (distance <= VP37_CURRENT_CONTROL_EDGE_GUARD_US) {
      ambiguous = true;
    }
    if ((age > latchAge) && (age < bestAge)) {
      matched = entry;
      bestAge = age;
    }
  }
  if (ambiguous || (bestAge >= (VP37_CURRENT_CONTROL_MAX_AGE_US +
                                (2U * sample->latchPeriodUs)))) {
    matched = NULL;
  }
  if (matched != NULL) {
    const int32_t difference = sample->latchedPwm - matched->pwm;
    if ((difference > VP37_CURRENT_CONTROL_DUTY_TOLERANCE) ||
        (difference < -VP37_CURRENT_CONTROL_DUTY_TOLERANCE)) {
      matched = NULL;
    }
  }
  return matched;
}

void VP37_updateCurrentControl(VP37Pump *self, float dt) {
  VP37CurrentControl *control = &self->currentControl;
  const VP37CurrentPulseResult *sample = &self->scan.cycleResult;
  if (self->demand.atRest || !isfinite(dt) || (dt <= 0.0f) ||
      (dt >= ((float)VP37_CURRENT_CONTROL_MAX_AGE_US * 0.000001f))) {
    VP37_resetCurrentControl(self);
  } else {
    const uint32_t now = hal_micros();
    control->sampleAgeUs =
        now - (sample->cycleStartUs + (sample->onTimeUs / 2U));
    const bool usable =
        control->enabled && self->thermal.observationEnabled &&
        !self->supply.frozen && self->scan.running &&
        (self->scan.cycleResultStatus == HAL_OK) && sample->waveformValid &&
        sample->zeroValid && sample->latchValid &&
        (sample->clippedSamples == 0U) && isfinite(sample->meanAmps) &&
        (sample->meanAmps >= VP37_DRIVE_MIN_CURRENT_A) &&
        (sample->onTimeUs > 0U) && (sample->onTimeUs < sample->latchPeriodUs) &&
        (sample->latchPeriodUs < VP37_CURRENT_CONTROL_MAX_AGE_US) &&
        (control->sampleAgeUs < VP37_CURRENT_CONTROL_MAX_AGE_US);
    const VP37CurrentCommand *command =
        usable ? VP37_findCurrentCommand(control, sample, now) : NULL;
    control->active = command != NULL;
    if (!control->active) {
      control->requestedPwm = 0.0f;
    } else if (!control->seen ||
               (sample->cycleStartUs != control->lastCycleUs)) {
      const float countsPerAmp =
          VP37_currentCountsPerAmp(command->voltageScale);
      control->sampleTargetAmps = command->nominalPwm / countsPerAmp;
      control->measuredAmps = sample->meanAmps;
      control->errorAmps = control->sampleTargetAmps - control->measuredAmps;
      control->requestedPwm = hal_constrain(
          VP37_CURRENT_CONTROL_GAIN * control->errorAmps * countsPerAmp,
          -VP37_CURRENT_CONTROL_LIMIT_PWM, VP37_CURRENT_CONTROL_LIMIT_PWM);
      control->lastCycleUs = sample->cycleStartUs;
      control->seen = true;
    } else {
      // Repeated periods only age; their error is never evaluated again.
    }
    const float step = VP37_CURRENT_CONTROL_SLEW_PWM_S * dt;
    control->correctionPwm += hal_constrain(
        control->requestedPwm - control->correctionPwm, -step, step);
  }
}
