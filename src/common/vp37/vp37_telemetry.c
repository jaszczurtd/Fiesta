// VP37 telemetry: the control sample, the console lines and the bench trace.
// The lines are printed on another core from a published snapshot; the trace
// is recorded on the control core. Nothing here drives the pump. Bench output
// only: without VP37_TELEMETRY_ENABLED the unit is empty.

#include "vp37_internal.h"
#include <hal/core/hal_text.h>
#include <math.h>

#if VP37_TELEMETRY_ENABLED

static VP37TraceSample VP37_controlSample(const VP37Telemetry *view);
static void VP37_showControlSample(const VP37TraceSample *sample,
                                   const char *kind);

void VP37_showDebug(const VP37Pump *self, const VP37Snapshot *snapshot) {
  const VP37Telemetry *const view = &snapshot->telemetry;
  const VP37TraceSample sample = VP37_controlSample(view);
  VP37_showControlSample(&sample, "C");

  static uint32_t lastTelemetryMs = 0U;
  if (hal_millis_interval_elapsed_now(&lastTelemetryMs,
                                      VP37_TELEMETRY_UPDATE)) {
    const char *const activeTest = view->activeTestName;
    const char *const testName = (activeTest != NULL) ? activeTest : "none";
    const uint32_t cycleDelayMs = view->cyclicDelayMs;
    deb("VP37 CFG rev:221 kp:%.4f ki:%.4f kd:%.5f topkd:%.5f dkeff:%.5f "
        "tf:%.4f tu:%.1f "
        "min:%d max:%d V:%.1f Vl:%.2f Ve:%.2f Vc:%.3f vg:%.4f vf:%.3f "
        "vcor:%.4f t:%.1fC imax:%.1f tw:%.2f "
        "tcf:%.4f mode:position test:%s cyclic_ms:%lu slew:%.1f "
        "upper_slew:%.1f "
        "ien:%u iconfirm:%lu vsync:%u vuse:%u vfrz:%u Vavg:%.3f vage:%lu "
        "vlead:%.3f vdot:%.2f pwm_hz:%u "
        "Imeas:%.4f Rdrv:%.4f rcf:%.4f ren:%u ruse:%u rn:%lu rhold:%u "
        "dbtop:%.0f db:%.0f mten:%u mtn:%lu mt50:%.1f mt90:%.1f mt100:%.1f "
        "mup:%.0f mdn:%.0f scan:%u fr:%lu blk:%lu gaps:%lu "
        "cen:%u cuse:%u ctar:%.4f cref:%.4f cerr:%.4f cpwm:%.2f cage:%lu "
        "Robs:%.4f rmatch:%u rquiet:%u rlearn:%u rlcnt:%lu scanus:%lu "
        "execus:%lu late:%lu ovr:%lu plim:%.1f stand_slew:%.1f "
        "stand_upper:%.1f adec:%.0f "
        "hold_in:%d stand_rise:%.2f stand_fall:%.2f tdec:%.0f mcap:%.0f "
        "hoff:%lu thold:%u ref:%u full:%u freeze:%u mgate:%u mvel:%.1f "
        "slowd:%u sd:%.3f beta:%.3f rkp:%.5f ierr:%.3f dramp:%u udb:%u "
        "rbrake:%u ileak:%u ibias:%.3f iphase:%u ipart:%.3f istart:%ld "
        "itau:%.3f ishare:%.3f ianchor:%.3f ihvalid:%u ihtar:%ld ihbias:%.3f "
        "ireuse:%u",
        view->pid.kp, view->pid.ki, view->pid.kd, view->pid.topKd,
        view->pid.effectiveKd, view->pid.tf, view->pidTimeUpdate,
        view->feedback.adjustMin, view->feedback.adjustMax,
        view->supply.lastVolts, view->supply.localVolts,
        view->supply.inputVolts, view->supply.heldVolts,
        view->supply.localScale, VP37_VOLTAGE_FILTER_S, view->supply.correction,
        view->thermal.lastFuelTemp, view->pid.integralLimit,
        view->thermal.temperatureCompensationWeight,
        view->thermal.temperatureCorrection, testName,
        (unsigned long)cycleDelayMs, VP37_DESIRED_SLEW_PERCENT_PER_SECOND,
        VP37_DESIRED_UPPER_SLEW_PERCENT_PER_SECOND,
        view->thermal.observationEnabled ? 1U : 0U,
        (unsigned long)view->pid.integralHoldConfirmMs,
        view->supply.cycleEnabled ? 1U : 0U, view->supply.cycleUsed ? 1U : 0U,
        view->supply.frozen ? 1U : 0U, view->supply.cycleVolts,
        (unsigned long)view->supply.cycleAgeUs, view->supply.predictionVolts,
        view->supply.voltageSlope, (unsigned)VP37_PWM_FREQUENCY_HZ,
        view->thermal.cycleAmps, view->thermal.driveResistance,
        view->thermal.driveCorrection,
        view->thermal.driveCompensationEnabled ? 1U : 0U,
        view->thermal.driveCompensationUsed ? 1U : 0U,
        (unsigned long)view->thermal.driveSamples,
        (view->thermal.driveVoltageSettled && view->currentControl.driveSettled)
            ? 0U
            : 1U,
        view->pid.integralDeadbandTopHz, view->pid.integralDeadbandHz,
        view->feedforward.mapTrimEnabled ? 1U : 0U,
        (unsigned long)view->feedforward.mapTrimTransfers,
        view->feedforward.mapTrim[5], view->feedforward.mapTrim[9],
        view->feedforward.mapTrim[10], view->feedforward.motionBoostUp,
        view->feedforward.motionBoostDown, view->scan.running ? 1U : 0U,
        (unsigned long)view->scan.frameNs, (unsigned long)view->scan.blocks,
        (unsigned long)view->scan.gaps, view->currentControl.enabled ? 1U : 0U,
        view->currentControl.active ? 1U : 0U, view->currentControl.targetAmps,
        view->currentControl.sampleTargetAmps, view->currentControl.errorAmps,
        view->currentControl.correctionPwm,
        (unsigned long)view->currentControl.sampleAgeUs,
        view->thermal.driveObservationOhms, view->thermal.cycleValid ? 1U : 0U,
        view->thermal.cycleSettled ? 1U : 0U,
        view->thermal.driveLearning ? 1U : 0U,
        (unsigned long)view->thermal.driveLearnedSamples,
        (unsigned long)view->scan.collectUs, (unsigned long)view->controlExecUs,
        (unsigned long)view->controlLateMaxUs,
        (unsigned long)view->controlOverruns, view->demand.physicalLimitPercent,
        VP37_STATIONARY_SLEW_PERCENT_PER_SECOND,
        VP37_STATIONARY_UPPER_SLEW_PERCENT_PER_SECOND,
        VP37_ARRIVAL_DECEL_PERCENT_PER_S2, VP37_INTEGRAL_HOLD_ENTER_HZ,
        VP37_STATIONARY_RISE_WEIGHT, VP37_STATIONARY_FALL_WEIGHT,
        view->demand.topArrivalDecel, view->feedforward.motionRateCap,
        (unsigned long)view->pid.integralHoldOffsetMs,
        view->thermal.scaleHoldEnabled ? 1U : 0U,
        view->pid.septemberReference ? 1U : 0U,
        view->pid.fullRangePrototype ? 1U : 0U,
        (unsigned int)view->pid.researchFreezeMask,
        view->pid.researchMotionIntegralGate ? 1U : 0U,
        view->pid.motionIntegralVelocity,
        view->pid.researchSlowDamping ? 1U : 0U, view->pid.slowDampingOutput,
        view->pid.researchReferenceWeight, view->pid.referenceKp,
        view->pid.integralInput, view->pid.researchRampDamping ? 1U : 0U,
        view->pid.researchUpperDeadband ? 1U : 0U,
        view->pid.researchRampBraking ? 1U : 0U,
        view->pid.researchDescentIntegralUnwind ? 1U : 0U,
        view->pid.motionIntegralBias,
        (unsigned int)view->pid.descentUnwindPhase,
        view->pid.descentUnwindTarget,
        (long)view->pid.motionIntegralStartPosition,
        VP37_DESCENT_INTEGRAL_UNWIND_S, view->pid.descentIntegralRetainedShare,
        view->pid.motionIntegralAnchor,
        view->pid.holdingIntegralReady ? 1U : 0U,
        (long)view->pid.holdingIntegralTarget, view->pid.holdingIntegralBias,
        view->pid.descentUnwindUsesHolding ? 1U : 0U);
    adjustometer_reading_t telemetry = view->adjustometer;
    const bool extendedFresh = VP37_readAdjustometerExtended(self, &telemetry);
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
 *   per, on   measured fall-to-fall period and ON time [us]
 *   duty      PWM command reconstructed from on / per
 *   latch, lp falling edge before ON and its fall-to-fall period [us]
 *   lduty, lv duty reconstructed from on / lp and valid latch timing
 *   n, gn     ON-phase samples acquired and left after the 60 us edge guards
 *   clip      samples that hit the ADC end stop
 *   zero, zv  shunt zero [ADC code] and whether it is valid
 *   valid     waveformValid: the ampere fields mean something
 *   status    hal_status_t of the reduction (HAL_EAGAIN: no complete period)
 *   Vavg, Vok full-period supply mean [V] and its own validity
 *   blk, gaps blocks retained since start and blocks the loop never saw
 *   dma, take completion/retention times of the latest DMA block [us]
 *   reduce    MCU time when this result became available [us]; subtract
 *             us+on to measure delivery delay after the ON phase
 *   pollus    CPU time retaining/tracking the latest block [us]
 *   match     cmd_us/cmd_pwm identify the command owning this observation
 *   gl        glitches: excursions across the gate hysteresis that ended
 *             before confirmation, counted since the last history reset;
 *             a gate-detection quality figure, read by nothing else
 *   ctar/cref newest/historical current target [A]; cerr historical error [A]
 *   cpwm      applied current correction [nominal PWM counts]
 *   cuse/cage matching current observation usable / ON-midpoint age [us] */
void VP37_showCurrentPulse(const VP37Snapshot *snapshot) {
  const VP37Telemetry *const view = &snapshot->telemetry;
  const VP37CurrentPulseResult *result = &view->scan.cycleResult;
  // A later PWM write invalidates the lookup cache, not the recorded match.
  const bool matched = view->currentControl.matchFound &&
                       (view->currentControl.matchedSampleSequence ==
                        view->scan.cycleResultSequence);
  deb("VP37 IPULSE us:%lu seq:%lu state_us:%lu pwm:%ld adj:%ld des:%ld "
      "V:%.3f FT:%.1f Ion:%.4f I95:%.4f Ipk:%.4f per:%lu on:%lu "
      "duty:%ld n:%lu gn:%lu clip:%lu zero:%u zv:%u valid:%u status:%d "
      "Vavg:%.4f Vok:%u blk:%lu gaps:%lu gl:%lu "
      "ctar:%.4f cref:%.4f cerr:%.4f cpwm:%.2f cuse:%u cage:%lu "
      "latch:%lu lp:%lu lduty:%ld lv:%u dma:%lu take:%lu reduce:%lu "
      "pollus:%lu match:%u cmd_us:%lu cmd_pwm:%ld",
      (unsigned long)result->cycleStartUs, (unsigned long)view->controlSequence,
      (unsigned long)view->controlLastUs, (long)view->output.finalPWM,
      (long)view->feedback.position, (long)view->demand.desired,
      view->supply.heldVolts, view->thermal.lastFuelTemp, result->meanAmps,
      result->p95Amps, result->peakAmps, (unsigned long)result->periodUs,
      (unsigned long)result->onTimeUs, (long)result->pwmCommand,
      (unsigned long)result->samples, (unsigned long)result->guardedSamples,
      (unsigned long)result->clippedSamples, (unsigned)result->zeroRaw,
      result->zeroValid ? 1U : 0U, result->waveformValid ? 1U : 0U,
      (int)view->scan.cycleResultStatus, result->supplyVolts,
      result->supplyValid ? 1U : 0U, (unsigned long)view->scan.blocks,
      (unsigned long)view->scan.gaps, (unsigned long)result->glitches,
      view->currentControl.targetAmps, view->currentControl.sampleTargetAmps,
      view->currentControl.errorAmps, view->currentControl.correctionPwm,
      view->currentControl.active ? 1U : 0U,
      (unsigned long)view->currentControl.sampleAgeUs,
      (unsigned long)result->latchUs, (unsigned long)result->latchPeriodUs,
      (long)result->latchedPwm, result->latchValid ? 1U : 0U,
      (unsigned long)result->scanCompletedUs,
      (unsigned long)result->scanCollectedUs,
      (unsigned long)view->scan.reducedUs, (unsigned long)result->scanPollUs,
      matched ? 1U : 0U,
      (unsigned long)(matched ? view->currentControl.matchedCommand.writtenUs
                              : 0U),
      (long)(matched ? view->currentControl.matchedCommand.pwm : 0));
  // Time bins preserve the ON ramp; the freewheel current is not measured.
  static uint32_t s_lastWaveMs;
  if (result->profileValid && hal_millis_interval_elapsed_now(
                                  &s_lastWaveMs, VP37_CURRENT_WAVE_REPORT_MS)) {
    deb("VP37 IWAVE us:%lu t0:%lu i0:%.4f t1:%lu i1:%.4f "
        "t2:%lu i2:%.4f t3:%lu i3:%.4f t4:%lu i4:%.4f "
        "t5:%lu i5:%.4f t6:%lu i6:%.4f t7:%lu i7:%.4f",
        (unsigned long)result->cycleStartUs,
        (unsigned long)result->profileUs[0], result->profileAmps[0],
        (unsigned long)result->profileUs[1], result->profileAmps[1],
        (unsigned long)result->profileUs[2], result->profileAmps[2],
        (unsigned long)result->profileUs[3], result->profileAmps[3],
        (unsigned long)result->profileUs[4], result->profileAmps[4],
        (unsigned long)result->profileUs[5], result->profileAmps[5],
        (unsigned long)result->profileUs[6], result->profileAmps[6],
        (unsigned long)result->profileUs[7], result->profileAmps[7]);
  }
}

void VP37_showTrace(const VP37TraceSample *sample) {
  VP37_showControlSample(sample, "T");
}

static VP37TraceSample VP37_controlSample(const VP37Telemetry *view) {
  const VP37TraceSample sample = {
      .us = view->controlLastUs,
      .dt = view->controlDtUs,
      .sequence = view->controlSequence,
      .requestedPercent = view->demand.requestedPercent,
      .target = view->demand.target,
      .desired = view->demand.desired,
      .measured = view->feedback.position,
      .pwm = view->output.finalPWM,
      .ff = view->feedforward.pwm,
      .low = view->pid.negativeLimit,
      .motionFF = view->feedforward.motion,
      .high = view->pid.upperLimit,
      .volts = view->supply.lastVolts,
      .localVolts = view->supply.localVolts,
      .compensationInputVolts = view->supply.inputVolts,
      .compensationVolts = view->supply.heldVolts,
      .voltageCorrection = view->supply.correction,
      .cycleVoltageUsed = view->supply.cycleUsed,
      .voltageOverRange = view->supply.overRange,
      .mapTrim = view->feedforward.mapTrimApplied,
      .thermalScale = view->thermal.scale,
      .integralHold = view->pid.integralHold,
      .upperHolding = view->pid.upperHoldingActive,
      .motionIntegralHold = view->pid.motionIntegralHold,
      .fuelTemp = view->thermal.lastFuelTemp,
      .temperatureCorrection = view->thermal.temperatureCorrection,
      .terms = view->pid.terms,
      .softFloor = view->pid.softFloorActive,
      .hardwareClamp = view->output.pwmLimited,
      .quantityAtRest = view->demand.atRest,
      .status = view->feedback.lastStatus,
      .rawHz = view->feedback.rawHz,
      .filteredHz = view->feedback.filteredHz,
      .sampleNumber = view->feedback.sampleNumber,
      .measuredUs = view->feedback.sampleUs,
      .ageUs = view->feedback.ageUs,
      .readStatus = view->feedback.readStatus,
      .readUs = view->feedback.readUs,
      .retries = view->feedback.retries,
      .fresh = view->feedback.fresh,
      .cyclicDelayMs = view->cyclicDelayMs,
      .pidDtUs = view->pidDtUs};
  return sample;
}

static void VP37_showControlSample(const VP37TraceSample *sample,
                                   const char *kind) {
  deb("VP37 %s us:%lu dt:%lu n:%lu thr:%.1f tar:%d des:%d adj:%d pwm:%d err:%d "
      "ff:%.1f P:%.1f I:%.1f D:%.1f raw:%.1f corr:%.1f lo:%.1f hi:%.1f "
      "sh:%d sl:%d sf:%d hw:%d rest:%d st:%u hzraw:%lu hz:%lu sn:%lu su:%lu "
      "age:%u io:%d ious:%lu retry:%u fresh:%d pdt:%lu V:%.1f Vl:%.2f "
      "Ve:%.2f Vc:%.3f vcor:%.4f ih:%d uh:%d cg:%d vp:%d vhi:%d "
      "ft:%.0f tcf:%.4f tsc:%.4f mff:%.1f cyms:%lu mtrim:%.1f",
      kind, (unsigned long)sample->us, (unsigned long)sample->dt,
      (unsigned long)sample->sequence, sample->requestedPercent, sample->target,
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
      sample->voltageCorrection, sample->integralHold, sample->upperHolding,
      sample->motionIntegralHold, sample->cycleVoltageUsed,
      sample->voltageOverRange, sample->fuelTemp, sample->temperatureCorrection,
      sample->thermalScale, sample->motionFF,
      (unsigned long)sample->cyclicDelayMs, sample->mapTrim);
}

/* Who holds the trace buffer. Only the control core moves IDLE ->
 * ARMED/RECORDING
 * -> READY and only the reading core moves READY -> IDLE, so each side acts on
 * the buffer alone between two release/acquire hand-overs. Streaming uses
 * count/next as SPSC indices: publish a slot, copy it, then release it. */
enum {
  VP37_TRACE_IDLE = 0U,
  VP37_TRACE_RECORDING,
  VP37_TRACE_READY,
  VP37_TRACE_ARMED,
  VP37_TRACE_CAL_RECORDING,
  VP37_TRACE_CAL_READY,
  VP37_TRACE_STREAM_RECORDING,
  VP37_TRACE_STREAM_READY
};

/* Each endpoint samples at most timeout / interval + 1 times. */
enum {
  VP37_CAL_TRACE_SAMPLES =
      2U *
      ((VP37_CALIBRATION_TIMEOUT_MS / VP37_CALIBRATION_SAMPLE_INTERVAL_MS) + 1U)
};
typedef struct {
  VP37CalibrationSample samples[VP37_CAL_TRACE_SAMPLES];
  VP37CalibrationResult result;
} VP37CalibrationTrace;
JB_STATIC_ASSERT(sizeof(VP37CalibrationTrace) <=
                     sizeof(VP37TraceSample) * VP37_TRACE_SAMPLES,
                 "Calibration must fit the existing trace buffer");
typedef struct {
  VP37MetricSample samples[VP37_TRACE_SAMPLES];
  VP37MetricResult result;
} VP37MetricTrace;
JB_STATIC_ASSERT(sizeof(VP37MetricTrace) <=
                     sizeof(VP37TraceSample) * VP37_TRACE_SAMPLES,
                 "Metrics must fit the existing trace buffer");

static struct {
  union {
    VP37TraceSample samples[VP37_TRACE_SAMPLES];
    VP37CalibrationTrace calibration;
    VP37MetricTrace metrics;
  } storage;
  uint32_t count; /**< Samples recorded; the control core's while recording. */
  uint32_t next;  /**< Next sample to read; the reading core's. */
  uint8_t state;  /**< VP37_TRACE_*, changed atomically. */
  uint32_t completedCount; /**< Reading core: count handed to the console. */
  bool endPending; /**< Reading core: completion has not been consumed. */
} s_trace;

static uint8_t VP37_traceState(void) {
  return HAL_ATOMIC_LOAD(&s_trace.state, HAL_ATOMIC_ACQUIRE);
}

hal_status_t VP37_startMetricStream(const VP37Pump *self) {
  if (self == NULL) {
    return HAL_EINVAL;
  }
  if (VP37_traceState() != VP37_TRACE_IDLE) {
    return HAL_EBUSY;
  }
  if (!self->vp37Initialized) {
    return HAL_EAGAIN;
  }
  s_trace.storage.metrics.result = (VP37MetricResult){0};
  s_trace.storage.metrics.result.startedUs = hal_micros();
  s_trace.count = 0U;
  s_trace.next = 0U;
  HAL_ATOMIC_STORE(&s_trace.state, (uint8_t)VP37_TRACE_STREAM_RECORDING,
                   HAL_ATOMIC_RELEASE);
  return HAL_OK;
}

hal_status_t VP37_stopMetricStream(void) {
  if (VP37_traceState() != VP37_TRACE_STREAM_RECORDING) {
    return HAL_ENOENT;
  }
  s_trace.storage.metrics.result.finishedUs = hal_micros();
  HAL_ATOMIC_STORE(&s_trace.state, (uint8_t)VP37_TRACE_STREAM_READY,
                   HAL_ATOMIC_RELEASE);
  return HAL_OK;
}

hal_status_t VP37_readMetricSample(VP37MetricSample *sample) {
  if (sample == NULL) {
    return HAL_EINVAL;
  }
  const uint8_t state = VP37_traceState();
  if ((state != VP37_TRACE_STREAM_RECORDING) &&
      (state != VP37_TRACE_STREAM_READY)) {
    return HAL_ENOENT;
  }
  const uint32_t next = HAL_ATOMIC_LOAD(&s_trace.next, HAL_ATOMIC_RELAXED);
  const uint32_t count = HAL_ATOMIC_LOAD(&s_trace.count, HAL_ATOMIC_ACQUIRE);
  if (next == count) {
    return state == VP37_TRACE_STREAM_RECORDING ? HAL_EAGAIN : HAL_ENOENT;
  }
  *sample = s_trace.storage.metrics.samples[next % VP37_TRACE_SAMPLES];
  HAL_ATOMIC_STORE(&s_trace.next, next + 1U, HAL_ATOMIC_RELEASE);
  return HAL_OK;
}

hal_status_t VP37_takeMetricResult(VP37MetricResult *result) {
  if (result == NULL) {
    return HAL_EINVAL;
  }
  if (VP37_traceState() != VP37_TRACE_STREAM_READY) {
    return HAL_EAGAIN;
  }
  if (HAL_ATOMIC_LOAD(&s_trace.next, HAL_ATOMIC_RELAXED) !=
      HAL_ATOMIC_LOAD(&s_trace.count, HAL_ATOMIC_ACQUIRE)) {
    return HAL_EAGAIN;
  }
  *result = s_trace.storage.metrics.result;
  s_trace.next = 0U;
  HAL_ATOMIC_STORE(&s_trace.state, (uint8_t)VP37_TRACE_IDLE,
                   HAL_ATOMIC_RELEASE);
  return HAL_OK;
}

static void VP37_recordMetricSample(void) {
  const uint32_t count = HAL_ATOMIC_LOAD(&s_trace.count, HAL_ATOMIC_RELAXED);
  const uint32_t next = HAL_ATOMIC_LOAD(&s_trace.next, HAL_ATOMIC_ACQUIRE);
  // End rather than overwrite a sample the reader still owns. Also end before
  // the audit's sample count could wrap (over 248 days at the default 200 Hz).
  if (((count - next) >= VP37_TRACE_SAMPLES) || (count == UINT32_MAX)) {
    s_trace.storage.metrics.result.overflow = true;
    (void)VP37_stopMetricStream();
    return;
  }
  const VP37Telemetry *const view = VP37_publishedTelemetry();
  const VP37MetricSample sample = {
      .us = view->controlLastUs,
      .dt = view->controlDtUs,
      .sequence = view->controlSequence,
      .target = view->demand.target,
      .desired = view->demand.desired,
      .measured = view->feedback.position,
      .pwm = view->output.finalPWM,
      .rawHz = view->feedback.rawHz,
      .filteredHz = view->feedback.filteredHz,
      .sampleNumber = view->feedback.sampleNumber,
      .measuredUs = view->feedback.sampleUs,
      .readUs = view->feedback.readUs,
      .pidDtUs = view->pidDtUs,
      .cyclicDelayMs = view->cyclicDelayMs,
      .driveSamples = view->thermal.driveSamples,
      .latchUs = view->scan.cycleResult.latchUs,
      .latchPeriodUs = view->scan.cycleResult.latchPeriodUs,
      .commandUs = view->currentControl.matchedCommand.writtenUs,
      .thermalScale = view->thermal.scale,
      .requestedPercent = view->demand.requestedPercent,
      .readStatus = view->feedback.readStatus,
      .ageUs = view->feedback.ageUs,
      .status = view->feedback.lastStatus,
      .quantityAtRest = view->demand.atRest,
      .fresh = view->feedback.fresh,
      .driveUsed = view->thermal.driveCompensationUsed,
      .latchValid = view->scan.cycleResult.latchValid,
      .commandMatched = view->currentControl.matchFound &&
                        (view->currentControl.matchedSampleSequence ==
                         view->scan.cycleResultSequence)};
  s_trace.storage.metrics.samples[count % VP37_TRACE_SAMPLES] = sample;
  if (count == 0U) {
    s_trace.storage.metrics.result.firstSequence = sample.sequence;
  }
  s_trace.storage.metrics.result.lastSequence = sample.sequence;
  s_trace.storage.metrics.result.samples = count + 1U;
  HAL_ATOMIC_STORE(&s_trace.count, count + 1U, HAL_ATOMIC_RELEASE);
}

typedef struct {
  char *cursor;
  size_t remaining;
  hal_status_t status;
} VP37MetricLine;

static void VP37_metricAppend(VP37MetricLine *line, const char *text) {
  if (line->status == HAL_OK) {
    line->status = hal_text_concat_ex(line->cursor, line->remaining, "", text);
    if (line->status == HAL_OK) {
      const size_t length = strlen(text);
      line->cursor += length;
      line->remaining -= length;
    }
  }
}

static void VP37_metricField(VP37MetricLine *line, const char *name,
                             const char *value, hal_status_t status) {
  if (status != HAL_OK) {
    line->status = status;
  }
  VP37_metricAppend(line, " ");
  VP37_metricAppend(line, name);
  VP37_metricAppend(line, ":");
  VP37_metricAppend(line, value);
}

static void VP37_metricUnsigned(VP37MetricLine *line, const char *name,
                                uint32_t value) {
  char text[HAL_TEXT_U32_DECIMAL_SIZE];
  const hal_status_t status = hal_text_format_u32_ex(value, text, sizeof(text));
  VP37_metricField(line, name, text, status);
}

static void VP37_metricSigned(VP37MetricLine *line, const char *name,
                              int32_t value) {
  char text[HAL_TEXT_I32_DECIMAL_SIZE];
  const hal_status_t status = hal_text_format_i32_ex(value, text, sizeof(text));
  VP37_metricField(line, name, text, status);
}

hal_status_t VP37_formatMetricSample(const VP37MetricSample *sample,
                                     char *buffer, size_t capacity) {
  if ((sample == NULL) || (buffer == NULL) || (capacity == 0U)) {
    return HAL_EINVAL;
  }
  if (!isfinite(sample->requestedPercent) ||
      (sample->requestedPercent < -1.0f) ||
      (sample->requestedPercent > 100.0f) || !isfinite(sample->thermalScale) ||
      (sample->thermalScale < 0.0f) || (sample->thermalScale > 10.0f)) {
    return HAL_EINVAL;
  }
  // Include CRLF for one serial write. Debug formatting would flush USB
  // separately for the prefix, fields and line ending.
  VP37MetricLine line = {
      .cursor = buffer, .remaining = capacity, .status = HAL_OK};
  buffer[0] = '\0';
  VP37_metricAppend(&line, "VP37 N");
  VP37_metricUnsigned(&line, "us", sample->us);
  VP37_metricUnsigned(&line, "dt", sample->dt);
  VP37_metricUnsigned(&line, "n", sample->sequence);
  VP37_metricSigned(&line, "thr10",
                    (int32_t)lroundf(sample->requestedPercent * 10.0f));
  VP37_metricSigned(&line, "tar", sample->target);
  VP37_metricSigned(&line, "des", sample->desired);
  VP37_metricSigned(&line, "adj", sample->measured);
  VP37_metricSigned(&line, "pwm", sample->pwm);
  VP37_metricUnsigned(&line, "hzraw", sample->rawHz);
  VP37_metricUnsigned(&line, "hz", sample->filteredHz);
  VP37_metricUnsigned(&line, "sn", sample->sampleNumber);
  VP37_metricUnsigned(&line, "su", sample->measuredUs);
  VP37_metricUnsigned(&line, "age", sample->ageUs);
  VP37_metricSigned(&line, "io", (int32_t)sample->readStatus);
  VP37_metricUnsigned(&line, "ious", sample->readUs);
  VP37_metricUnsigned(&line, "fresh", sample->fresh ? 1U : 0U);
  VP37_metricUnsigned(&line, "st", sample->status);
  VP37_metricUnsigned(&line, "rest", sample->quantityAtRest ? 1U : 0U);
  VP37_metricUnsigned(&line, "pdt", sample->pidDtUs);
  VP37_metricUnsigned(&line, "cyms", sample->cyclicDelayMs);
  VP37_metricUnsigned(&line, "rn", sample->driveSamples);
  VP37_metricUnsigned(&line, "ruse", sample->driveUsed ? 1U : 0U);
  VP37_metricSigned(&line, "tsc10000",
                    (int32_t)lroundf(sample->thermalScale * 10000.0f));
  VP37_metricUnsigned(&line, "latch", sample->latchUs);
  VP37_metricUnsigned(&line, "lp", sample->latchPeriodUs);
  VP37_metricUnsigned(&line, "lv", sample->latchValid ? 1U : 0U);
  VP37_metricUnsigned(&line, "cmd_us", sample->commandUs);
  VP37_metricUnsigned(&line, "match", sample->commandMatched ? 1U : 0U);
  VP37_metricAppend(&line, "\r\n");
  return line.status;
}

void VP37_showMetricResult(const VP37MetricResult *result) {
  deb("VP37 STREAM END start:%lu end:%lu samples:%lu first:%lu last:%lu "
      "overflow:%u",
      (unsigned long)result->startedUs, (unsigned long)result->finishedUs,
      (unsigned long)result->samples, (unsigned long)result->firstSequence,
      (unsigned long)result->lastSequence, result->overflow ? 1U : 0U);
}

hal_status_t VP37_calibrationTraceBegin(void) {
  if (VP37_traceState() != VP37_TRACE_IDLE) {
    return HAL_EBUSY;
  }
  s_trace.count = 0U;
  s_trace.storage.calibration.result = (VP37CalibrationResult){0};
  s_trace.storage.calibration.result.startedUs = hal_micros();
  HAL_ATOMIC_STORE(&s_trace.state, (uint8_t)VP37_TRACE_CAL_RECORDING,
                   HAL_ATOMIC_RELEASE);
  return HAL_OK;
}

void VP37_calibrationTraceRecord(const adjustometer_reading_t *reading,
                                 uint16_t pwm) {
  if (VP37_traceState() != VP37_TRACE_CAL_RECORDING) {
    return;
  }
  if (s_trace.count < COUNTOF(s_trace.storage.calibration.samples)) {
    VP37CalibrationSample *const sample =
        &s_trace.storage.calibration.samples[s_trace.count];
    sample->reading = *reading;
    sample->us = hal_micros();
    sample->pwm = pwm;
    s_trace.count++;
  } else {
    s_trace.storage.calibration.result.overflow = true;
  }
}

void VP37_calibrationTraceEnd(const VP37Pump *self, bool minSettled,
                              bool maxSettled) {
  if (VP37_traceState() == VP37_TRACE_CAL_RECORDING) {
    VP37CalibrationResult *const result = &s_trace.storage.calibration.result;
    result->finishedUs = hal_micros();
    result->samples = s_trace.count;
    result->minimum = self->feedback.adjustMin;
    result->maximum = self->feedback.adjustMax;
    result->minSettled = minSettled;
    result->maxSettled = maxSettled;
    result->valid = self->feedback.calibrationDone;
    HAL_ATOMIC_STORE(&s_trace.state, (uint8_t)VP37_TRACE_CAL_READY,
                     HAL_ATOMIC_RELEASE);
  }
}

hal_status_t VP37_readCalibrationTrace(VP37CalibrationSample *sample) {
  if (sample == NULL) {
    return HAL_EINVAL;
  }
  const uint8_t state = VP37_traceState();
  if (state == VP37_TRACE_CAL_RECORDING) {
    return HAL_EAGAIN;
  }
  if ((state != VP37_TRACE_CAL_READY) || (s_trace.next >= s_trace.count)) {
    return HAL_ENOENT;
  }
  *sample = s_trace.storage.calibration.samples[s_trace.next];
  s_trace.next++;
  return HAL_OK;
}

hal_status_t VP37_takeCalibrationResult(VP37CalibrationResult *result) {
  if (result == NULL) {
    return HAL_EINVAL;
  }
  if ((VP37_traceState() != VP37_TRACE_CAL_READY) ||
      (s_trace.next < s_trace.count)) {
    return HAL_EAGAIN;
  }
  *result = s_trace.storage.calibration.result;
  s_trace.next = 0U;
  HAL_ATOMIC_STORE(&s_trace.state, (uint8_t)VP37_TRACE_IDLE,
                   HAL_ATOMIC_RELEASE);
  return HAL_OK;
}

void VP37_showCalibration(void) {
  bool connected = false;
  if ((hal_usb_cdc_is_connected(&connected) != HAL_OK) || !connected ||
      hal_debug_is_muted()) {
    return;
  }
  VP37CalibrationSample sample;
  for (uint8_t i = 0U; i < 2U; ++i) {
    if (VP37_readCalibrationTrace(&sample) != HAL_OK) {
      break;
    }
    const adjustometer_reading_t *const r = &sample.reading;
    deb("VP37 CAL us:%lu phase:%s pwm:%u adj:%d hzraw:%lu hz:%lu bl:%lu "
        "sn:%lu su:%lu age:%u st:%u io:%d comm:%u fresh:%u v:%u ft:%u",
        (unsigned long)sample.us, sample.pwm == 0U ? "MIN" : "MAX",
        (unsigned int)sample.pwm, r->pulseHz, (unsigned long)r->rawHz,
        (unsigned long)r->signalHz, (unsigned long)r->baselineHz,
        (unsigned long)r->sampleNumber, (unsigned long)r->measuredUs,
        (unsigned int)r->ageUs, (unsigned int)r->status, (int)r->readStatus,
        r->commOk ? 1U : 0U, r->feedbackFresh ? 1U : 0U,
        (unsigned int)r->voltageRaw, (unsigned int)r->fuelTempC);
  }
  VP37CalibrationResult result;
  if (VP37_takeCalibrationResult(&result) == HAL_OK) {
    deb("VP37 CAL END start:%lu end:%lu samples:%lu min:%ld max:%ld "
        "minok:%u maxok:%u valid:%u overflow:%u pwm:0",
        (unsigned long)result.startedUs, (unsigned long)result.finishedUs,
        (unsigned long)result.samples, (long)result.minimum,
        (long)result.maximum, result.minSettled ? 1U : 0U,
        result.maxSettled ? 1U : 0U, result.valid ? 1U : 0U,
        result.overflow ? 1U : 0U);
  }
}

static hal_status_t VP37_startTraceMode(const VP37Pump *self, bool descent) {
  if (self == NULL) {
    return HAL_EINVAL;
  }
  if (VP37_traceState() != VP37_TRACE_IDLE) {
    return HAL_EBUSY;
  }
  if (!self->vp37Initialized) {
    return HAL_EAGAIN;
  }
  if (descent && (!self->pid.fullRangePrototype ||
                  !self->pid.researchMotionIntegralGate)) {
    return HAL_ESTATE;
  }
  s_trace.count = 0U;
  HAL_ATOMIC_STORE(&s_trace.state,
                   (uint8_t)(descent ? VP37_TRACE_ARMED : VP37_TRACE_RECORDING),
                   HAL_ATOMIC_RELEASE);
  return HAL_OK;
}

hal_status_t VP37_startTrace(const VP37Pump *self) {
  return VP37_startTraceMode(self, false);
}

hal_status_t VP37_startTraceOnDescent(const VP37Pump *self) {
  return VP37_startTraceMode(self, true);
}

bool VP37_traceCapturing(void) {
  const uint8_t state = VP37_traceState();
  return (state == VP37_TRACE_RECORDING) || (state == VP37_TRACE_ARMED);
}

hal_status_t VP37_readTrace(VP37TraceSample *sample) {
  if (sample == NULL) {
    return HAL_EINVAL;
  }
  const uint8_t state = VP37_traceState();
  if ((state == VP37_TRACE_RECORDING) || (state == VP37_TRACE_ARMED)) {
    return HAL_EAGAIN;
  }
  hal_status_t status = HAL_ENOENT;
  if (state == VP37_TRACE_READY) {
    if (s_trace.next < s_trace.count) {
      *sample = s_trace.storage.samples[s_trace.next];
      s_trace.next++;
      status = HAL_OK;
    }
    // Hand the buffer back after the last sample, or at once when empty.
    if (s_trace.next >= s_trace.count) {
      s_trace.completedCount = s_trace.count;
      s_trace.endPending = true;
      s_trace.next = 0U;
      HAL_ATOMIC_STORE(&s_trace.state, (uint8_t)VP37_TRACE_IDLE,
                       HAL_ATOMIC_RELEASE);
    }
  }
  return status;
}

hal_status_t VP37_takeTraceCompletion(uint32_t *count) {
  if (count == NULL) {
    return HAL_EINVAL;
  }
  if (!s_trace.endPending) {
    return HAL_EAGAIN;
  }
  *count = s_trace.completedCount;
  s_trace.endPending = false;
  return HAL_OK;
}

void VP37_traceStop(void) {
  (void)VP37_stopMetricStream();
  if (VP37_traceCapturing()) {
    HAL_ATOMIC_STORE(&s_trace.state, (uint8_t)VP37_TRACE_READY,
                     HAL_ATOMIC_RELEASE);
  }
}

/**
 * @brief Append the control sample of this step to a running trace.
 * @note The trace stops by itself when the buffer is full or the pump has
 * been stopped, so a reader never waits on a recording that cannot end.
 */
void VP37_traceRecord(const VP37Pump *self) {
  const uint8_t state = VP37_traceState();
  if (state == VP37_TRACE_STREAM_RECORDING) {
    VP37_recordMetricSample();
    return;
  }
  const bool released = (state == VP37_TRACE_ARMED) && self->demand.atRest;
  if (state == VP37_TRACE_ARMED) {
    const bool moving = self->pid.motionIntegralHold && (self->pid.error < 0) &&
                        (self->demand.desired == self->demand.target) &&
                        VP37_hasMeasuredDescent(self);
    if (!moving && self->vp37Initialized && !released) {
      // Keep the preceding step without allocating another trace buffer.
      s_trace.storage.samples[0] =
          VP37_controlSample(VP37_publishedTelemetry());
      s_trace.count = 1U;
      return;
    }
    HAL_ATOMIC_STORE(&s_trace.state, (uint8_t)VP37_TRACE_RECORDING,
                     HAL_ATOMIC_RELEASE);
  }
  if ((state == VP37_TRACE_RECORDING) || (state == VP37_TRACE_ARMED)) {
    s_trace.storage.samples[s_trace.count] =
        VP37_controlSample(VP37_publishedTelemetry());
    s_trace.count++;
    if ((s_trace.count == COUNTOF(s_trace.storage.samples)) ||
        !self->vp37Initialized || released) {
      VP37_traceStop();
    }
  }
}

#endif /* VP37_TELEMETRY_ENABLED */
