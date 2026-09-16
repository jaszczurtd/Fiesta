// VP37 telemetry: the control sample, the console lines and the bench trace.
// Runs on core 0 from a snapshot of the pump; nothing here drives it.

#include "vp37_internal.h"

static VP37TraceSample VP37_controlSample(const VP37Pump *self);
static void VP37_showControlSample(const VP37TraceSample *sample,
                                   const char *kind);

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

#if ECU_FUNCTIONAL_TESTS_ENABLED
void VP37_showTrace(const VP37TraceSample *sample) {
  VP37_showControlSample(sample, "T");
}
#endif

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

/**
 * @brief Append the control sample of this step to a running trace.
 * @param self VP37 controller instance to sample.
 * @return None.
 * @note The trace stops by itself when the buffer is full or the pump has
 * been stopped, so a reader never waits on a recording that cannot end.
 */
void VP37_traceRecord(const VP37Pump *self) {
  if (s_trace.recording) {
    s_trace.samples[s_trace.count++] = VP37_controlSample(self);
    if ((s_trace.count == COUNTOF(s_trace.samples)) || !self->vp37Initialized) {
      s_trace.recording = false;
    }
  }
}
#endif
