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
        self->pid.kp, self->pid.ki, self->pid.kd, self->pid.tf,
        self->pidTimeUpdate, self->feedback.adjustMin, self->feedback.adjustMax,
        self->supply.lastVolts, self->supply.localVolts,
        self->supply.inputVolts, self->supply.heldVolts,
        self->supply.localScale, VP37_VOLTAGE_FILTER_S, self->supply.correction,
        self->thermal.lastFuelTemp, self->pid.integralLimit,
        self->thermal.temperatureCompensationWeight,
        self->thermal.temperatureCorrection, mode, (unsigned long)cycleDelayMs,
        VP37_DESIRED_SLEW_PERCENT_PER_SECOND,
        VP37_DESIRED_UPPER_SLEW_PERCENT_PER_SECOND,
        self->thermal.observationEnabled ? 1U : 0U,
        (unsigned long)self->pid.integralHoldConfirmMs,
        self->supply.cycleEnabled ? 1U : 0U, self->supply.cycleUsed ? 1U : 0U,
        self->supply.frozen ? 1U : 0U, self->supply.cycleVolts,
        (unsigned)VP37_PWM_FREQUENCY_HZ, self->thermal.cycleAmps,
        self->thermal.driveResistance, self->thermal.driveCorrection,
        self->thermal.driveCompensationEnabled ? 1U : 0U,
        self->thermal.driveCompensationUsed ? 1U : 0U,
        (unsigned long)self->thermal.driveSamples,
        self->pid.integralDeadbandTopHz, self->pid.integralDeadbandHz,
        self->feedforward.mapTrimEnabled ? 1U : 0U,
        (unsigned long)self->feedforward.mapTrimTransfers,
        self->feedforward.mapTrim[5], self->feedforward.mapTrim[9],
        self->feedforward.mapTrim[10], self->feedforward.motionBoostUp,
        self->feedforward.motionBoostDown, self->scan.running ? 1U : 0U,
        (unsigned long)self->scan.frameNs, (unsigned long)self->scan.blocks,
        (unsigned long)self->scan.gaps);
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

/* Fields of the `VP37 IPULSE` line, one per reduced scan block:
 *   us        start of the measured PWM period (hal_micros)
 *   seq       control step that reduced the block; state_us its MCU time
 *   pwm       command live at the report; adj / des measured and slewed
 *             position [Hz]
 *   V, FT     supply used by the loop [V] and fuel temperature [C]
 *   Ion       P95-winsorized mean of the guarded ON phase [A]; I95 its
 *             95th percentile; Ipk the raw ON-phase maximum, spike-prone
 *   per, on   measured rise-to-rise period and ON time [us]
 *   duty      PWM command reconstructed from on / per
 *   n, gn     ON-phase samples acquired and left after the 60 us edge guards
 *   clip      samples that hit the ADC end stop
 *   zero, zv  shunt zero [ADC code] and whether it is valid
 *   valid     waveformValid: the ampere fields mean something
 *   status    hal_status_t of the reduction (HAL_EAGAIN: no complete period)
 *   Vavg, Vok full-period supply mean [V] and its own validity
 *   blk, gaps blocks reduced since start and blocks the loop never saw
 *   gl        glitches: excursions across the gate hysteresis that ended
 *             before the confirmation time, counted over the whole block;
 *             a gate-detection quality figure, read by nothing else */
void VP37_showCurrentPulse(const VP37Pump *self) {
  const VP37CurrentPulseResult *result = &self->scan.cycleResult;
  deb("VP37 IPULSE us:%lu seq:%lu state_us:%lu pwm:%ld adj:%ld des:%ld "
      "V:%.3f FT:%.1f Ion:%.4f I95:%.4f Ipk:%.4f per:%lu on:%lu "
      "duty:%ld n:%lu gn:%lu clip:%lu zero:%u zv:%u valid:%u status:%d "
      "Vavg:%.4f Vok:%u blk:%lu gaps:%lu gl:%lu",
      (unsigned long)result->cycleStartUs, (unsigned long)self->controlSequence,
      (unsigned long)self->controlLastUs, (long)self->output.finalPWM,
      (long)self->feedback.position, (long)self->demand.desired,
      self->supply.heldVolts, self->thermal.lastFuelTemp, result->meanAmps,
      result->p95Amps, result->peakAmps, (unsigned long)result->periodUs,
      (unsigned long)result->onTimeUs, (long)result->pwmCommand,
      (unsigned long)result->samples, (unsigned long)result->guardedSamples,
      (unsigned long)result->clippedSamples, (unsigned)result->zeroRaw,
      result->zeroValid ? 1U : 0U, result->waveformValid ? 1U : 0U,
      (int)self->scan.cycleResultStatus, result->supplyVolts,
      result->supplyValid ? 1U : 0U, (unsigned long)self->scan.blocks,
      (unsigned long)self->scan.gaps, (unsigned long)result->glitches);
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
      .throttle = self->demand.lastThrottle,
      .target = self->demand.target,
      .desired = self->demand.desired,
      .measured = self->feedback.position,
      .pwm = self->output.finalPWM,
      .ff = self->feedforward.pwm,
      .low = self->pid.negativeLimit,
      .motionFF = self->feedforward.motion,
      .high = self->pid.upperLimit,
      .volts = self->supply.lastVolts,
      .localVolts = self->supply.localVolts,
      .compensationInputVolts = self->supply.inputVolts,
      .compensationVolts = self->supply.heldVolts,
      .voltageCorrection = self->supply.correction,
      .cycleVoltageUsed = self->supply.cycleUsed,
      .voltageOverRange = self->supply.overRange,
      .mapTrim = self->feedforward.mapTrimApplied,
      .thermalScale = self->thermal.scale,
      .integralHold = self->pid.integralHold,
      .fuelTemp = self->thermal.lastFuelTemp,
      .temperatureCorrection = self->thermal.temperatureCorrection,
      .terms = self->pid.terms,
      .softFloor = self->pid.softFloorActive,
      .hardwareClamp = self->output.pwmLimited,
      .quantityAtRest = self->demand.atRest,
      .status = self->feedback.lastStatus,
      .rawHz = self->feedback.rawHz,
      .filteredHz = self->feedback.filteredHz,
      .sampleNumber = self->feedback.sampleNumber,
      .measuredUs = self->feedback.sampleUs,
      .ageUs = self->feedback.ageUs,
      .readStatus = self->feedback.readStatus,
      .readUs = self->feedback.readUs,
      .retries = self->feedback.retries,
      .fresh = self->feedback.fresh,
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
