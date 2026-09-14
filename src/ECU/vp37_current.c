#include "vp37_current.h"

#include "../common/fiesta_sensor_helpers.h"
#include "hardwareConfig.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define VP37_CURRENT_ACTIVE_THRESHOLD_RAW 64U
#define VP37_CURRENT_MIN_ACTIVE_SAMPLES 8U
#define VP37_CURRENT_MIN_ACTIVE_RUN 4U
#define VP37_CURRENT_ZERO_SAMPLES 256U
#define VP37_CURRENT_ZERO_MAX_RAW 128U
#define VP37_CURRENT_CAPTURE_SAMPLES 1250U
#define VP37_CURRENT_SAMPLE_DELAY_US 20U

/* A run this far off the commanded duty means the window is fragmented. */
#define VP37_CURRENT_RUN_PLAUSIBLE_LOW 0.6f
#define VP37_CURRENT_RUN_PLAUSIBLE_HIGH 1.6f

/* Gate-edge search budget, a few PWM periods at 200 Hz. */
#define VP37_CURRENT_PHASE_EDGE_TIMEOUT_US 30000U
/* An ON phase starting above these bounds means the coil kept conducting. */
#define VP37_CURRENT_PHASE_CCM_FRACTION 0.25f
#define VP37_CURRENT_PHASE_CCM_MIN_AMPS 0.5f
/* Cycles whose period strays this far from the median are dropped. */
#define VP37_CURRENT_PHASE_PERIOD_TOLERANCE 0.25f

/* Volts per ADC code, and the current one code represents on the shunt. */
#define VP37_CURRENT_VOLTS_PER_CODE (3.3f / (float)VP37_CURRENT_ADC_MAX_RAW)
#define VP37_CURRENT_AMPS_PER_CODE                                             \
  (VP37_CURRENT_VOLTS_PER_CODE / VP37_CURRENT_SHUNT_OHMS)

/*
 * The histogram and the phase buffer are never live at the same time: an
 * aggregate window reduces samples on the fly, a phase capture keeps them raw.
 * Overlaying them keeps the bench build at one 8 KiB buffer instead of two.
 */
typedef union {
  uint16_t histogram[VP37_CURRENT_ADC_BINS];
  VP37CurrentPhaseSample phase[VP37_CURRENT_PHASE_SAMPLES];
} vp37_current_storage_t;

typedef struct {
  uint64_t rawSum;
  uint64_t rawActiveSum;
  uint64_t rawSquareSum;
  uint32_t samples;
  uint32_t activeSamples;
  uint32_t activeRun;
  uint32_t maxActiveRun;
  uint32_t clippedSamples;
  uint32_t startedUs;
  uint16_t rawMin;
  uint16_t rawMax;
  bool started;
} vp37_current_state_t;

static vp37_current_state_t s_currentState;
static vp37_current_storage_t s_storage;
static uint32_t s_phaseCount;
static uint16_t s_currentZeroRaw;
static bool s_currentZeroValid;

static void VP37_currentSenseReset(void) {
  (void)memset(&s_currentState, 0, sizeof(s_currentState));
  (void)memset(&s_storage, 0, sizeof(s_storage));
  s_currentState.rawMin = UINT16_MAX;
  s_phaseCount = 0U;
}

static float VP37_currentRawToVolts(int raw) {
  float volts = 0.0f;
  const hal_status_t status = fiesta_adc_to_voltage_ex(raw, 0.0f, 1.0f, &volts);
  if (status != HAL_OK) {
    volts = 0.0f;
  }
  return volts;
}

float VP37_currentRawToAmps(uint16_t raw) {
  return VP37_currentRawToVolts((int)raw) / VP37_CURRENT_SHUNT_OHMS;
}

static uint16_t VP37_currentReadRaw(void) {
  int raw = hal_adc_compensate_rp2040_12bit(hal_adc_read(ADC_VP37_CURRENT_PIN));
  if (raw < 0) {
    raw = 0;
  } else if (raw > (int)VP37_CURRENT_ADC_MAX_RAW) {
    raw = (int)VP37_CURRENT_ADC_MAX_RAW;
  } else {
    // Reading is already inside the supported 12-bit range.
  }
  return (uint16_t)raw;
}

/** Subtract the calibrated zero and report whether the code hit the end stop.
 */
static uint16_t VP37_currentCorrectedRaw(bool *out_clipped) {
  const uint16_t raw = VP37_currentReadRaw();
  if (out_clipped != NULL) {
    *out_clipped = raw >= VP37_CURRENT_ADC_MAX_RAW;
  }
  uint16_t offset = 0U;
  if (s_currentZeroValid) {
    offset = s_currentZeroRaw;
  }
  uint16_t sample = 0U;
  if (raw > offset) {
    sample = (uint16_t)(raw - offset);
  }
  return sample;
}

void VP37_currentSenseInit(void) {
  VP37_currentSenseReset();
  s_currentZeroRaw = 0U;
  s_currentZeroValid = false;

  // Configure GPIO26 as an ADC input, then find the PWM-off median. Reject an
  // implausibly high zero so a wiring fault remains visible in telemetry.
  (void)VP37_currentReadRaw();
  for (uint32_t i = 0U; i < VP37_CURRENT_ZERO_SAMPLES; i++) {
    const uint16_t sample = VP37_currentReadRaw();
    s_storage.histogram[sample]++;
    hal_delay_us(VP37_CURRENT_SAMPLE_DELAY_US);
  }

  const uint32_t medianTarget = (VP37_CURRENT_ZERO_SAMPLES / 2U) + 1U;
  uint32_t cumulativeSamples = 0U;
  uint16_t zeroCandidate = 0U;
  for (uint32_t raw = 0U; raw < VP37_CURRENT_ADC_BINS; raw++) {
    cumulativeSamples += s_storage.histogram[raw];
    if (cumulativeSamples >= medianTarget) {
      zeroCandidate = (uint16_t)raw;
      break;
    }
  }
  s_currentZeroRaw = zeroCandidate;
  s_currentZeroValid = zeroCandidate <= VP37_CURRENT_ZERO_MAX_RAW;
  VP37_currentSenseReset();
}

void VP37_currentSenseSample(void) {
  bool clipped = false;
  const uint16_t sample = VP37_currentCorrectedRaw(&clipped);

  if (!s_currentState.started) {
    s_currentState.startedUs = hal_micros();
    s_currentState.started = true;
  }
  if (s_currentState.samples >= UINT16_MAX) {
    return;
  }

  s_currentState.rawSum += sample;
  s_currentState.rawSquareSum += (uint64_t)sample * (uint64_t)sample;
  s_currentState.samples++;
  if (clipped) {
    s_currentState.clippedSamples++;
  }
  if (sample < s_currentState.rawMin) {
    s_currentState.rawMin = sample;
  }
  if (sample > s_currentState.rawMax) {
    s_currentState.rawMax = sample;
  }
  if (s_storage.histogram[sample] != UINT16_MAX) {
    s_storage.histogram[sample]++;
  }
  if (sample >= VP37_CURRENT_ACTIVE_THRESHOLD_RAW) {
    s_currentState.rawActiveSum += sample;
    s_currentState.activeSamples++;
    s_currentState.activeRun++;
    if (s_currentState.activeRun > s_currentState.maxActiveRun) {
      s_currentState.maxActiveRun = s_currentState.activeRun;
    }
  } else {
    s_currentState.activeRun = 0U;
  }
}

hal_status_t VP37_currentHistogramPercentile(const uint16_t *histogram,
                                             uint32_t bins, uint32_t fromBin,
                                             uint32_t population,
                                             uint8_t percentile,
                                             uint16_t *out_bin) {
  if ((histogram == NULL) || (out_bin == NULL) || (bins == 0U) ||
      (population == 0U) || (fromBin >= bins) || (percentile == 0U) ||
      (percentile > 100U)) {
    return HAL_EINVAL;
  }

  const uint32_t target =
      (uint32_t)((((uint64_t)population * (uint64_t)percentile) + 99U) / 100U);
  uint32_t counted = 0U;
  for (uint32_t bin = fromBin; bin < bins; bin++) {
    counted += histogram[bin];
    if (counted >= target) {
      *out_bin = (uint16_t)bin;
      return HAL_OK;
    }
  }
  // Fewer samples than declared; report the highest populated bin.
  *out_bin = (uint16_t)(bins - 1U);
  return HAL_OK;
}

uint32_t VP37_currentExpectedActiveRun(int32_t pwmCommand, uint32_t samples,
                                       uint32_t windowUs) {
  if ((pwmCommand <= 0) || (samples == 0U) || (windowUs == 0U)) {
    return 0U;
  }
  int32_t command = pwmCommand;
  if (command > PWM_RESOLUTION) {
    command = PWM_RESOLUTION;
  }
  const float duty = (float)command / (float)PWM_RESOLUTION;
  const float periodUs = 1000000.0f / (float)VP37_PWM_FREQUENCY_HZ;
  const float samplesPerPeriod = ((float)samples * periodUs) / (float)windowUs;
  const float expected = duty * samplesPerPeriod;
  if (expected < 1.0f) {
    return 0U;
  }
  return (uint32_t)expected;
}

hal_status_t
VP37_currentSenseSnapshotEx(const VP37CurrentConditions *conditions,
                            VP37CurrentReading *out) {
  if (out == NULL) {
    return HAL_EINVAL;
  }
  if (s_currentState.samples == 0U) {
    return HAL_EAGAIN;
  }

  (void)memset(out, 0, sizeof(*out));
  out->conditions.pwmCommand = -1;
  out->conditions.measuredHz = -1;
  out->conditions.desiredHz = -1;
  if (conditions != NULL) {
    out->conditions = *conditions;
  }
  out->samples = s_currentState.samples;
  out->activeSamples = s_currentState.activeSamples;
  out->maxActiveRun = s_currentState.maxActiveRun;
  out->clippedSamples = s_currentState.clippedSamples;
  out->windowUs = hal_micros() - s_currentState.startedUs;
  out->zeroRaw = s_currentZeroRaw;
  out->zeroValid = s_currentZeroValid;
  out->rawMin = s_currentState.rawMin;
  out->rawMax = s_currentState.rawMax;
  out->rawMean = (float)s_currentState.rawSum / (float)s_currentState.samples;

  uint64_t meanRawWide = s_currentState.rawSum + (s_currentState.samples / 2U);
  meanRawWide /= s_currentState.samples;
  const uint16_t meanRaw = (uint16_t)meanRawWide;
  const float meanVolts = VP37_currentRawToVolts((int)meanRaw);
  out->peakVolts = VP37_currentRawToVolts(s_currentState.rawMax);
  out->peakAmps = out->peakVolts / VP37_CURRENT_SHUNT_OHMS;

  if (s_currentState.activeSamples != 0U) {
    out->activePercent = ((float)s_currentState.activeSamples * 100.0f) /
                         (float)s_currentState.samples;
  }

  out->expectedActiveRun = VP37_currentExpectedActiveRun(
      out->conditions.pwmCommand, out->samples, out->windowUs);
  if (out->expectedActiveRun != 0U) {
    out->activeRunRatio =
        (float)out->maxActiveRun / (float)out->expectedActiveRun;
    out->activeRunPlausible =
        (out->activeRunRatio >= VP37_CURRENT_RUN_PLAUSIBLE_LOW) &&
        (out->activeRunRatio <= VP37_CURRENT_RUN_PLAUSIBLE_HIGH);
  }

  if (s_currentZeroValid &&
      (s_currentState.activeSamples >= VP37_CURRENT_MIN_ACTIVE_SAMPLES) &&
      (s_currentState.maxActiveRun >= VP37_CURRENT_MIN_ACTIVE_RUN)) {
    out->switchMeanAmps = meanVolts / VP37_CURRENT_SHUNT_OHMS;
    out->rawActiveMean = (float)s_currentState.rawActiveSum /
                         (float)s_currentState.activeSamples;
    uint64_t activeMeanRawWide =
        s_currentState.rawActiveSum + (s_currentState.activeSamples / 2U);
    activeMeanRawWide /= s_currentState.activeSamples;
    const uint16_t activeMeanRaw = (uint16_t)activeMeanRawWide;
    out->activeMeanAmps =
        VP37_currentRawToVolts((int)activeMeanRaw) / VP37_CURRENT_SHUNT_OHMS;

    // Percentiles of the active samples describe the ON-phase waveform shape:
    // evenly spaced values mean a ramp, values bunched near P95 mean the
    // current saturates early in the ON phase.
    const struct {
      uint8_t percentile;
      uint16_t *rawField;
      float *ampsField;
    } wanted[] = {
        {5U, &out->rawP05, &out->p05Amps},  {25U, &out->rawP25, &out->p25Amps},
        {50U, &out->rawP50, &out->p50Amps}, {75U, &out->rawP75, &out->p75Amps},
        {95U, &out->rawP95, &out->p95Amps},
    };
    for (size_t i = 0U; i < COUNTOF(wanted); i++) {
      uint16_t bin = 0U;
      if (VP37_currentHistogramPercentile(
              s_storage.histogram, VP37_CURRENT_ADC_BINS,
              VP37_CURRENT_ACTIVE_THRESHOLD_RAW, s_currentState.activeSamples,
              wanted[i].percentile, &bin) == HAL_OK) {
        *wanted[i].rawField = bin;
        *wanted[i].ampsField = VP37_currentRawToAmps(bin);
      }
    }

    // Mean of the square over the whole window, so the OFF phase counts as
    // zero. This is the shunt RMS and the source resistor loss, never the coil
    // RMS: the freewheel current bypasses the shunt.
    const float meanSquareCodes =
        (float)s_currentState.rawSquareSum / (float)s_currentState.samples;
    const float rmsCodes = sqrtf(meanSquareCodes);
    out->rmsShuntAmps = rmsCodes * VP37_CURRENT_AMPS_PER_CODE;
    out->shuntPowerWatts =
        out->rmsShuntAmps * out->rmsShuntAmps * VP37_CURRENT_SHUNT_OHMS;
  }

  VP37_currentSenseReset();
  return HAL_OK;
}

hal_status_t VP37_currentSenseSnapshot(VP37CurrentReading *out) {
  return VP37_currentSenseSnapshotEx(NULL, out);
}

hal_status_t VP37_currentSenseCaptureEx(const VP37CurrentConditions *conditions,
                                        VP37CurrentReading *out) {
  if (out == NULL) {
    return HAL_EINVAL;
  }

  VP37_currentSenseReset();
  for (uint32_t i = 0U; i < VP37_CURRENT_CAPTURE_SAMPLES; i++) {
    VP37_currentSenseSample();
    hal_delay_us(VP37_CURRENT_SAMPLE_DELAY_US);
  }
  return VP37_currentSenseSnapshotEx(conditions, out);
}

hal_status_t VP37_currentSenseCapture(VP37CurrentReading *out) {
  return VP37_currentSenseCaptureEx(NULL, out);
}

/** One PWM cycle reduced to the facts the roadmap needs. */
typedef struct {
  uint32_t periodUs;
  uint32_t onTimeUs;
  uint32_t clipped;
  float startAmps;
  float endAmps;
  float riseAmpsPerMs;
  float chargeAmpMs;
  float squareIntegral; /**< A^2 * us over the whole cycle. */
} vp37_phase_cycle_t;

/** Median of a small array. Sorts in place; the caller owns a scratch copy. */
static float VP37_phaseMedian(float *values, uint32_t count) {
  if ((values == NULL) || (count == 0U)) {
    return 0.0f;
  }
  for (uint32_t i = 1U; i < count; i++) {
    const float key = values[i];
    uint32_t j = i;
    while ((j > 0U) && (values[j - 1U] > key)) {
      values[j] = values[j - 1U];
      j--;
    }
    values[j] = key;
  }
  if ((count % 2U) != 0U) {
    return values[count / 2U];
  }
  return (values[(count / 2U) - 1U] + values[count / 2U]) * 0.5f;
}

/** Reduce the samples of one cycle to its electrical facts. */
static void VP37_phaseReduceCycle(const VP37CurrentPhaseSample *samples,
                                  uint32_t first, uint32_t last,
                                  vp37_phase_cycle_t *cycle) {
  (void)memset(cycle, 0, sizeof(*cycle));
  cycle->periodUs = samples[last].timestampUs - samples[first].timestampUs;

  uint32_t lastOn = first;
  bool sawOn = false;
  float chargeAmpUs = 0.0f;
  for (uint32_t i = first; i < last; i++) {
    const float amps = VP37_currentRawToAmps(samples[i].rawSample);
    const float nextAmps = VP37_currentRawToAmps(samples[i + 1U].rawSample);
    const float dtUs =
        (float)(samples[i + 1U].timestampUs - samples[i].timestampUs);
    cycle->squareIntegral +=
        (((amps * amps) + (nextAmps * nextAmps)) * 0.5f) * dtUs;
    if (samples[i].gateOn != 0U) {
      sawOn = true;
      lastOn = i;
      chargeAmpUs += ((amps + nextAmps) * 0.5f) * dtUs;
    }
    if (samples[i].clipped != 0U) {
      cycle->clipped++;
    }
  }

  if (!sawOn) {
    return;
  }
  cycle->onTimeUs = samples[lastOn].timestampUs - samples[first].timestampUs;
  cycle->startAmps = VP37_currentRawToAmps(samples[first].rawSample);
  cycle->endAmps = VP37_currentRawToAmps(samples[lastOn].rawSample);
  cycle->chargeAmpMs = chargeAmpUs * 0.001f;
  if (cycle->onTimeUs != 0U) {
    cycle->riseAmpsPerMs = ((cycle->endAmps - cycle->startAmps) * 1000.0f) /
                           (float)cycle->onTimeUs;
  }
}

hal_status_t VP37_currentPhaseAnalyze(const VP37CurrentPhaseSample *samples,
                                      uint32_t count,
                                      VP37CurrentPhaseResult *out) {
  if ((samples == NULL) || (out == NULL)) {
    return HAL_EINVAL;
  }
  (void)memset(out, 0, sizeof(*out));
  if (count < 2U) {
    return HAL_EAGAIN;
  }

  // Gate turn-on edges bound the cycles. A capture that starts on an edge
  // contributes its first sample as one.
  uint32_t edges[VP37_CURRENT_PHASE_MAX_CYCLES + 1U];
  uint32_t edgeCount = 0U;
  if (samples[0].gateOn != 0U) {
    edges[edgeCount] = 0U;
    edgeCount++;
  }
  for (uint32_t i = 1U; (i < count) && (edgeCount < COUNTOF(edges)); i++) {
    if ((samples[i].gateOn != 0U) && (samples[i - 1U].gateOn == 0U)) {
      edges[edgeCount] = i;
      edgeCount++;
    }
  }
  if (edgeCount < 2U) {
    return HAL_EAGAIN;
  }

  vp37_phase_cycle_t cycles[VP37_CURRENT_PHASE_MAX_CYCLES];
  uint32_t cycleCount = 0U;
  for (uint32_t i = 0U; (i + 1U) < edgeCount; i++) {
    VP37_phaseReduceCycle(samples, edges[i], edges[i + 1U],
                          &cycles[cycleCount]);
    if ((cycles[cycleCount].periodUs != 0U) &&
        (cycles[cycleCount].onTimeUs != 0U)) {
      cycleCount++;
    } else {
      out->rejectedCycles++;
    }
  }
  if (cycleCount == 0U) {
    return HAL_EAGAIN;
  }

  // Drop cycles whose period strays from the median: the PWM command changed
  // mid-capture, or a sample was lost.
  float scratch[VP37_CURRENT_PHASE_MAX_CYCLES];
  for (uint32_t i = 0U; i < cycleCount; i++) {
    scratch[i] = (float)cycles[i].periodUs;
  }
  const float medianPeriod = VP37_phaseMedian(scratch, cycleCount);
  const float lowBound =
      medianPeriod * (1.0f - VP37_CURRENT_PHASE_PERIOD_TOLERANCE);
  const float highBound =
      medianPeriod * (1.0f + VP37_CURRENT_PHASE_PERIOD_TOLERANCE);

  uint32_t kept = 0U;
  float squareSum = 0.0f;
  float timeSum = 0.0f;
  for (uint32_t i = 0U; i < cycleCount; i++) {
    const float period = (float)cycles[i].periodUs;
    if ((period < lowBound) || (period > highBound)) {
      out->rejectedCycles++;
      continue;
    }
    cycles[kept] = cycles[i];
    squareSum += cycles[i].squareIntegral;
    timeSum += period;
    out->clippedSamples += cycles[i].clipped;
    kept++;
  }
  if (kept == 0U) {
    return HAL_EAGAIN;
  }
  out->cycles = kept;

  for (uint32_t i = 0U; i < kept; i++) {
    scratch[i] = (float)cycles[i].periodUs;
  }
  out->medianPeriodUs = (uint32_t)VP37_phaseMedian(scratch, kept);
  for (uint32_t i = 0U; i < kept; i++) {
    scratch[i] = (float)cycles[i].onTimeUs;
  }
  out->medianOnTimeUs = (uint32_t)VP37_phaseMedian(scratch, kept);
  for (uint32_t i = 0U; i < kept; i++) {
    scratch[i] = cycles[i].startAmps;
  }
  out->startAmps = VP37_phaseMedian(scratch, kept);
  for (uint32_t i = 0U; i < kept; i++) {
    scratch[i] = cycles[i].endAmps;
  }
  out->endAmps = VP37_phaseMedian(scratch, kept);
  for (uint32_t i = 0U; i < kept; i++) {
    scratch[i] = cycles[i].riseAmpsPerMs;
  }
  out->riseAmpsPerMs = VP37_phaseMedian(scratch, kept);
  for (uint32_t i = 0U; i < kept; i++) {
    scratch[i] = cycles[i].chargeAmpMs;
  }
  out->chargeAmpMs = VP37_phaseMedian(scratch, kept);

  if (timeSum > 0.0f) {
    out->rmsShuntAmps = sqrtf(squareSum / timeSum);
    out->shuntPowerWatts =
        out->rmsShuntAmps * out->rmsShuntAmps * VP37_CURRENT_SHUNT_OHMS;
  }

  // A pulse that starts from a substantial current means the coil kept
  // conducting through the freewheel path, so its mean current is far above
  // the chopped shunt mean.
  out->continuousConduction =
      (out->startAmps >= VP37_CURRENT_PHASE_CCM_MIN_AMPS) &&
      (out->startAmps >= (out->endAmps * VP37_CURRENT_PHASE_CCM_FRACTION));
  return HAL_OK;
}

const VP37CurrentPhaseSample *VP37_currentPhaseSamples(uint32_t *out_count) {
  if (out_count == NULL) {
    return NULL;
  }
  *out_count = s_phaseCount;
  if (s_phaseCount == 0U) {
    return NULL;
  }
  return s_storage.phase;
}

hal_status_t
VP37_currentSensePhaseCapture(const VP37CurrentConditions *conditions,
                              VP37CurrentPhaseResult *out) {
  if (out == NULL) {
    return HAL_EINVAL;
  }
  (void)memset(out, 0, sizeof(*out));
  out->conditions.pwmCommand = -1;
  out->conditions.measuredHz = -1;
  out->conditions.desiredHz = -1;
  if (conditions != NULL) {
    out->conditions = *conditions;
  }

  VP37_currentSenseReset();

  // Wait for a gate turn-on edge so the buffer starts at a known phase.
  const uint32_t waitStart = hal_micros();
  bool previousOn = !hal_gpio_read(PIO_VP37_RPM);
  bool aligned = false;
  while ((hal_micros() - waitStart) < VP37_CURRENT_PHASE_EDGE_TIMEOUT_US) {
    const bool gateOn = !hal_gpio_read(PIO_VP37_RPM);
    if (gateOn && !previousOn) {
      aligned = true;
      break;
    }
    previousOn = gateOn;
    // Keep the poll coarse enough to advance the time source on every turn.
    hal_delay_us(1U);
  }
  if (!aligned) {
    return HAL_ETIMEOUT;
  }

  for (uint32_t i = 0U; i < VP37_CURRENT_PHASE_SAMPLES; i++) {
    bool clipped = false;
    const bool gateOn = !hal_gpio_read(PIO_VP37_RPM);
    const uint16_t sample = VP37_currentCorrectedRaw(&clipped);
    s_storage.phase[i].timestampUs = hal_micros();
    s_storage.phase[i].rawSample = sample;
    s_storage.phase[i].gateOn = gateOn ? 1U : 0U;
    s_storage.phase[i].clipped = clipped ? 1U : 0U;
    hal_delay_us(VP37_CURRENT_SAMPLE_DELAY_US);
  }
  s_phaseCount = VP37_CURRENT_PHASE_SAMPLES;

  // The analysis clears its output, so the conditions are restored after it.
  const VP37CurrentConditions recorded = out->conditions;
  const hal_status_t status =
      VP37_currentPhaseAnalyze(s_storage.phase, s_phaseCount, out);
  out->conditions = recorded;
  return status;
}
