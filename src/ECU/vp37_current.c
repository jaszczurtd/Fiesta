#include "vp37_current.h"

#include "../common/fiesta_sensor_helpers.h"
#include "config.h"
#include "hardwareConfig.h"

#include <string.h>

#define VP37_CURRENT_ZERO_SAMPLES 256U
#define VP37_CURRENT_ZERO_MAX_RAW 128U
#define VP37_CURRENT_SAMPLE_DELAY_US 20U
#define VP37_CURRENT_PULSE_EDGE_GUARD_US 60U
#define VP37_CURRENT_PULSE_MIN_GUARDED_SAMPLES 8U
#define VP37_CURRENT_PULSE_PERIOD_TOLERANCE 0.20f
#define VP37_CURRENT_SUPPLY_MIN_PER_PHASE 2U
/* Completion timestamps include interrupt-entry jitter, not sampling gaps. */
#define VP37_CURRENT_SCAN_TIMESTAMP_TOLERANCE_US 100U

static uint16_t s_currentZeroRaw;
static bool s_currentZeroValid;

/* Both halves of the scan block live here; the scan owns them while it runs. */
static uint16_t
    s_scanBuffer[2U * VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_PINS]
    __attribute__((aligned(4)));
static uint8_t s_scanShuntPosition;
static uint8_t s_scanSupplyPosition;
static uint16_t
    s_scanHistory[VP37_CURRENT_SCAN_HISTORY_FRAMES * VP37_CURRENT_SCAN_PINS];
static uint32_t s_scanHistoryFrames;
static uint32_t s_scanHistoryStartUs;
static uint32_t s_scanHistorySequence;
static uint32_t s_scanHistoryCompletedUs;
static uint32_t s_scanHistoryCollectedUs;
static uint32_t s_scanHistoryPollUs;
static uint32_t s_scanHistoryFrameNs;
static bool s_scanPending;
typedef struct {
  uint32_t run, glitches, rise, previousFall;
  uint32_t periodRise, periodFall, periodLatch;
  bool initialized, gateOn, haveRise, havePreviousFall, found;
} VP37CurrentEdges;
static VP37CurrentEdges s_scanEdges;
static uint32_t s_scanFirstFrame;
static VP37CurrentPulseResult s_scanPulse;
static uint32_t s_scanPulseFall;
static bool s_scanPulseCached;

/* The short ON ramp is nearly sorted; also used for the startup median. */
static void VP37_currentSortRaw(uint16_t *values, uint32_t count) {
  for (uint32_t i = 1U; i < count; i++) {
    const uint16_t key = values[i];
    uint32_t j = i;
    while ((j > 0U) && (values[j - 1U] > key)) {
      values[j] = values[j - 1U];
      j--;
    }
    values[j] = key;
  }
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

uint16_t VP37_currentAmpsToRaw(float amps) {
  float raw = (amps * VP37_CURRENT_SHUNT_OHMS *
               (float)VP37_CURRENT_ADC_MAX_RAW / 3.3f) +
              0.5f;
  if (!(raw > 0.0f)) {
    raw = 0.0f; // also NaN
  } else if (raw > (float)VP37_CURRENT_ADC_MAX_RAW) {
    raw = (float)VP37_CURRENT_ADC_MAX_RAW;
  } else {
    // Inside the 12-bit range.
  }
  return (uint16_t)raw;
}

/** Compensate the RP2040 transfer gaps and clamp to the 12-bit range. */
static uint16_t VP37_currentCompensatedRaw(int reading) {
  int raw = hal_adc_compensate_rp2040_12bit(reading);
  if (raw < 0) {
    raw = 0;
  } else if (raw > (int)VP37_CURRENT_ADC_MAX_RAW) {
    raw = (int)VP37_CURRENT_ADC_MAX_RAW;
  } else {
    // Reading is already inside the supported 12-bit range.
  }
  return (uint16_t)raw;
}

static uint16_t VP37_currentReadRaw(void) {
  return VP37_currentCompensatedRaw(hal_adc_read(ADC_VP37_CURRENT_PIN));
}

/** Subtract the calibrated zero and report whether the code hit the end stop.
 */
static uint16_t VP37_currentCorrectedRaw(uint16_t raw, bool *out_clipped) {
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
  uint16_t samples[VP37_CURRENT_ZERO_SAMPLES];
  s_currentZeroRaw = 0U;
  s_currentZeroValid = false;
  (void)VP37_currentReadRaw();
  for (uint32_t i = 0U; i < COUNTOF(samples); i++) {
    samples[i] = VP37_currentReadRaw();
    hal_delay_us(VP37_CURRENT_SAMPLE_DELAY_US);
  }
  VP37_currentSortRaw(samples, COUNTOF(samples));
  s_currentZeroRaw = samples[COUNTOF(samples) / 2U];
  s_currentZeroValid = s_currentZeroRaw <= VP37_CURRENT_ZERO_MAX_RAW;
}

static bool VP37_currentPeriodPlausible(uint32_t periodUs) {
  const uint32_t expectedPeriodUs = 1000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ;
  const float lowPeriod =
      (float)expectedPeriodUs * (1.0f - VP37_CURRENT_PULSE_PERIOD_TOLERANCE);
  const float highPeriod =
      (float)expectedPeriodUs * (1.0f + VP37_CURRENT_PULSE_PERIOD_TOLERANCE);
  return ((float)periodUs >= lowPeriod) && ((float)periodUs <= highPeriod);
}

static int32_t VP37_currentDutyFromTime(uint32_t onTimeUs, uint32_t periodUs) {
  uint64_t pwm = ((uint64_t)onTimeUs * (uint64_t)PWM_RESOLUTION) +
                 ((uint64_t)periodUs / 2U);
  pwm /= periodUs;
  if (pwm > (uint64_t)PWM_RESOLUTION) {
    pwm = PWM_RESOLUTION;
  }
  return (int32_t)pwm;
}

hal_status_t VP37_currentPulseAnalyze(const VP37CurrentPhaseSample *samples,
                                      uint32_t count, uint32_t cycleStartUs,
                                      uint32_t onTimeUs, uint32_t periodUs,
                                      VP37CurrentPulseResult *out) {
  if ((samples == NULL) || (out == NULL) || (count == 0U) ||
      (onTimeUs < (2U * VP37_CURRENT_PULSE_EDGE_GUARD_US)) ||
      (periodUs <= onTimeUs)) {
    return HAL_EINVAL;
  }

  if (count > VP37_CURRENT_PULSE_SAMPLES) {
    return HAL_EOVERFLOW;
  }

  (void)memset(out, 0, sizeof(*out));
  out->samples = count;
  out->cycleStartUs = cycleStartUs;
  out->periodUs = periodUs;
  out->onTimeUs = onTimeUs;
  out->zeroRaw = s_currentZeroRaw;
  out->zeroValid = s_currentZeroValid;

  uint16_t guarded[VP37_CURRENT_PULSE_SAMPLES];
  uint32_t guardedCount = 0U;
  uint16_t peakRaw = 0U;
  uint32_t profileRaw[VP37_CURRENT_PROFILE_BINS] = {0U};
  uint32_t profileTime[VP37_CURRENT_PROFILE_BINS] = {0U};
  uint32_t profileCount[VP37_CURRENT_PROFILE_BINS] = {0U};
  const uint32_t guardedUs =
      onTimeUs - (2U * VP37_CURRENT_PULSE_EDGE_GUARD_US) + 1U;
  for (uint32_t i = 0U; i < count; i++) {
    if (samples[i].rawSample > VP37_CURRENT_ADC_MAX_RAW) {
      return HAL_EINVAL;
    }
    if (samples[i].clipped != 0U) {
      out->clippedSamples++;
    }
    if (samples[i].rawSample > peakRaw) {
      peakRaw = samples[i].rawSample;
    }
    const uint32_t phaseUs = samples[i].timestampUs - cycleStartUs;
    if ((samples[i].gateOn != 0U) &&
        (phaseUs >= VP37_CURRENT_PULSE_EDGE_GUARD_US) &&
        (phaseUs <= (onTimeUs - VP37_CURRENT_PULSE_EDGE_GUARD_US))) {
      guarded[guardedCount] = samples[i].rawSample;
      guardedCount++;
      const uint32_t bin = ((phaseUs - VP37_CURRENT_PULSE_EDGE_GUARD_US) *
                            VP37_CURRENT_PROFILE_BINS) /
                           guardedUs;
      profileRaw[bin] += samples[i].rawSample;
      profileTime[bin] += phaseUs;
      profileCount[bin]++;
    }
  }
  out->guardedSamples = guardedCount;
  out->peakAmps = VP37_currentRawToAmps(peakRaw);
  if (guardedCount < VP37_CURRENT_PULSE_MIN_GUARDED_SAMPLES) {
    return HAL_EAGAIN;
  }

  VP37_currentSortRaw(guarded, guardedCount);
  const uint32_t p95Rank = (((guardedCount * 95U) + 99U) / 100U) - 1U;
  const uint16_t p95Raw = guarded[p95Rank];
  uint32_t winsorizedSum = 0U;
  for (uint32_t i = 0U; i < guardedCount; i++) {
    winsorizedSum += (guarded[i] < p95Raw) ? guarded[i] : p95Raw;
  }
  const uint16_t meanRaw =
      (uint16_t)((winsorizedSum + (guardedCount / 2U)) / guardedCount);
  out->meanAmps = VP37_currentRawToAmps(meanRaw);
  out->p95Amps = VP37_currentRawToAmps(p95Raw);

  out->pwmCommand = VP37_currentDutyFromTime(onTimeUs, periodUs);
  out->waveformValid = out->zeroValid && (out->clippedSamples == 0U) &&
                       VP37_currentPeriodPlausible(periodUs);
  out->profileValid = out->waveformValid;
  for (uint32_t bin = 0U; bin < VP37_CURRENT_PROFILE_BINS; ++bin) {
    const uint32_t n = profileCount[bin];
    if (n == 0U) {
      out->profileValid = false;
    } else {
      out->profileAmps[bin] =
          VP37_currentRawToAmps((uint16_t)((profileRaw[bin] + n / 2U) / n));
      out->profileUs[bin] = (profileTime[bin] + n / 2U) / n;
    }
  }
  return HAL_OK;
}

hal_status_t VP37_currentScanStart(void) {
  hal_adc_scan_config_t config;
  (void)memset(&config, 0, sizeof(config));
  config.pins[0] = ADC_VP37_CURRENT_PIN;
  config.pins[1] = ADC_SENSORS_PIN;
  config.pins[2] = ADC_VOLT_PIN;
  config.pin_count = VP37_CURRENT_SCAN_PINS;
  config.conversion_period_ns = VP37_CURRENT_SCAN_CONVERSION_NS;
  config.buffer = s_scanBuffer;
  config.block_frames = VP37_CURRENT_SCAN_BLOCK_FRAMES;
  const hal_status_t status = hal_adc_scan_start(&config);
  if (status == HAL_OK) {
    s_scanHistoryFrames = 0U;
    s_scanPending = false;
    s_scanShuntPosition = hal_adc_scan_pin_position(ADC_VP37_CURRENT_PIN);
    s_scanSupplyPosition = hal_adc_scan_pin_position(ADC_VOLT_PIN);
  }
  return status;
}

hal_status_t VP37_currentScanStop(void) {
  const hal_status_t status = hal_adc_scan_stop();
  if (status == HAL_OK) {
    s_scanHistoryFrames = 0U;
    s_scanPending = false;
  }
  return status;
}

uint32_t VP37_currentScanFrameNs(void) {
  return hal_adc_scan_frame_period_ns();
}

static uint32_t VP37_currentFramesToUs(uint32_t frames, uint32_t frameNs) {
  return (uint32_t)((((uint64_t)frames * (uint64_t)frameNs) + 500U) / 1000U);
}

static uint16_t VP37_currentBlockRaw(const VP37CurrentScanBlock *block,
                                     uint32_t frame, uint8_t position,
                                     bool compensated) {
  const uint16_t raw = block->samples[frame * block->pinCount + position];
  return compensated ? raw : VP37_currentCompensatedRaw((int)raw);
}

/* Edges span DMA boundaries. Absolute frame indices wrap by subtraction. */
static void VP37_currentTrackEdges(const VP37CurrentScanBlock *block,
                                   uint32_t firstFrame, VP37CurrentEdges *edges,
                                   bool compensated) {
  const uint16_t onRaw = VP37_currentAmpsToRaw(VP37_CURRENT_GATE_ON_AMPS);
  const uint16_t offRaw = VP37_currentAmpsToRaw(VP37_CURRENT_GATE_OFF_AMPS);
  for (uint32_t k = 0U; k < block->frames; k++) {
    const uint16_t level = VP37_currentCorrectedRaw(
        VP37_currentBlockRaw(block, k, block->shuntPosition, compensated),
        NULL);
    if (!edges->initialized) {
      edges->gateOn = level >= onRaw;
      edges->initialized = true;
      continue;
    }
    const bool crossed = edges->gateOn ? (level <= offRaw) : (level >= onRaw);
    if (!crossed && (edges->run != 0U)) {
      edges->glitches++;
    }
    edges->run = crossed ? (edges->run + 1U) : 0U;
    if (edges->run < VP37_CURRENT_GATE_CONFIRM_FRAMES) {
      continue;
    }
    const uint32_t edge =
        firstFrame + k - (VP37_CURRENT_GATE_CONFIRM_FRAMES - 1U);
    edges->run = 0U;
    edges->gateOn = !edges->gateOn;
    if (edges->gateOn) {
      edges->rise = edge;
      edges->haveRise = true;
    } else {
      if (edges->haveRise && edges->havePreviousFall) {
        edges->found = true;
        edges->periodRise = edges->rise;
        edges->periodFall = edge;
        edges->periodLatch = edges->previousFall;
      }
      edges->haveRise = false;
      edges->previousFall = edge;
      edges->havePreviousFall = true;
    }
  }
}

static hal_status_t
VP37_currentScanReducePeriod(const VP37CurrentScanBlock *block,
                             VP37CurrentPulseResult *out,
                             const VP37CurrentEdges *retainedEdges,
                             uint32_t firstFrame, bool compensated) {
  static VP37CurrentPhaseSample s_pulseSamples[VP37_CURRENT_PULSE_SAMPLES];
  if (out == NULL) {
    return HAL_EINVAL;
  }
  (void)memset(out, 0, sizeof(*out));
  out->zeroRaw = s_currentZeroRaw;
  out->zeroValid = s_currentZeroValid;
  if ((block == NULL) || (block->samples == NULL) || (block->frames < 2U) ||
      (block->pinCount == 0U) || (block->shuntPosition >= block->pinCount) ||
      (block->supplyPosition >= block->pinCount) || (block->frameNs == 0U)) {
    return HAL_EINVAL;
  }
  if (!s_currentZeroValid) {
    return HAL_ESTATE;
  }

  VP37CurrentEdges localEdges = {0};
  const VP37CurrentEdges *edges = retainedEdges;
  if (edges == NULL) {
    VP37_currentTrackEdges(block, firstFrame, &localEdges, compensated);
    edges = &localEdges;
  }
  const uint32_t glitches = edges->glitches;
  out->glitches = glitches;
  const uint32_t periodRise = edges->periodRise - firstFrame;
  const uint32_t periodFall = edges->periodFall - firstFrame;
  const uint32_t periodLatch = edges->periodLatch - firstFrame;
  if (!edges->found || (periodLatch >= block->frames) ||
      (periodRise >= block->frames) || (periodFall >= block->frames)) {
    return HAL_EAGAIN;
  }
  const uint32_t onFrames = periodFall - periodRise;
  if (onFrames > VP37_CURRENT_PULSE_SAMPLES) {
    return HAL_EOVERFLOW;
  }
  const uint32_t onTimeUs = VP37_currentFramesToUs(onFrames, block->frameNs);
  const uint32_t periodUs =
      VP37_currentFramesToUs(periodFall - periodLatch, block->frameNs);
  const uint32_t cycleStartUs =
      block->startUs + VP37_currentFramesToUs(periodRise, block->frameNs);

  uint32_t count = 0U;
  uint32_t supplySum[2] = {0U, 0U};
  uint32_t supplyCount[2] = {0U, 0U};
  uint32_t supplyRejected = 0U;
  const uint32_t frameUs =
      ((block->frameNs % 1000U) == 0U) ? (block->frameNs / 1000U) : 0U;
  for (uint32_t k = periodLatch; k < periodFall; k++) {
    const bool on = k >= periodRise;
    if (on) {
      bool clipped = false;
      // Whole-microsecond frames need no 64-bit division per ON sample.
      // The uint32_t product retains the timestamp's modulo wrap.
      const uint32_t sampleUs = (frameUs != 0U)
                                    ? (k * frameUs)
                                    : VP37_currentFramesToUs(k, block->frameNs);
      s_pulseSamples[count].timestampUs = block->startUs + sampleUs;
      s_pulseSamples[count].rawSample = VP37_currentCorrectedRaw(
          VP37_currentBlockRaw(block, k, block->shuntPosition, compensated),
          &clipped);
      s_pulseSamples[count].gateOn = 1U;
      s_pulseSamples[count].clipped = clipped ? 1U : 0U;
      count++;
    }
    // Each phase gets its own mean because the rail sags while the gate drives.
    const int supplyRaw =
        (int)VP37_currentBlockRaw(block, k, block->supplyPosition, compensated);
    if ((supplyRaw > 0) && (supplyRaw < (int)VP37_CURRENT_ADC_MAX_RAW)) {
      supplySum[on ? 1U : 0U] += (uint32_t)supplyRaw;
      supplyCount[on ? 1U : 0U]++;
    } else {
      supplyRejected++;
    }
  }

  hal_status_t status = VP37_currentPulseAnalyze(
      s_pulseSamples, count, cycleStartUs, onTimeUs, periodUs, out);
  if (status == HAL_EINVAL) {
    status = HAL_EAGAIN; // an ON phase shorter than both edge guards
  }
  if (status != HAL_OK) {
    // Analysis clears the result; restore what the block still knows.
    out->zeroRaw = s_currentZeroRaw;
    out->zeroValid = s_currentZeroValid;
    out->periodUs = periodUs;
    out->onTimeUs = onTimeUs;
    out->cycleStartUs = cycleStartUs;
    out->waveformValid = false;
  }
  out->glitches = glitches;

  // With the active-low driver, ON occupies the end of the hardware period.
  out->latchUs =
      block->startUs + VP37_currentFramesToUs(periodLatch, block->frameNs);
  out->latchPeriodUs = periodUs;
  out->latchValid =
      VP37_currentPeriodPlausible(periodUs) && (onTimeUs < periodUs);
  if (out->latchValid) {
    out->latchedPwm = VP37_currentDutyFromTime(onTimeUs, periodUs);
  }

  // The supply mean has its own sample budget and quality rule, so a current
  // result that is rejected must not discard it.
  float supplyVolts = 0.0f;
  bool supplyValid = false;
  if (VP37_currentPeriodPlausible(periodUs) && (supplyRejected == 0U) &&
      (supplyCount[0] >= VP37_CURRENT_SUPPLY_MIN_PER_PHASE) &&
      (supplyCount[1] >= VP37_CURRENT_SUPPLY_MIN_PER_PHASE)) {
    const float onFraction = (float)onTimeUs / (float)periodUs;
    const float rawMean =
        ((float)supplySum[1] / (float)supplyCount[1]) * onFraction +
        ((float)supplySum[0] / (float)supplyCount[0]) * (1.0f - onFraction);
    supplyValid =
        fiesta_adc_to_voltage_ex((int)(rawMean + 0.5f), (float)V_DIVIDER_R1,
                                 (float)V_DIVIDER_R2, &supplyVolts) == HAL_OK;
  }
  out->supplySamples = supplyCount[0] + supplyCount[1];
  out->supplyVolts = supplyVolts;
  out->supplyValid = supplyValid;
  return status;
}

/* The newest complete time window rejects PWM ripple without waiting for a
   current edge. Its timestamp describes the mean, not its publication. */
static void VP37_currentReduceLatestSupply(const VP37CurrentScanBlock *block,
                                           VP37CurrentPulseResult *out,
                                           bool compensated) {
  const uint32_t periodNs =
      VP37_currentPeriodPlausible(out->periodUs)
          ? (out->periodUs * 1000U)
          : (1000000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ);
  const uint32_t frames = (periodNs + (block->frameNs / 2U)) / block->frameNs;
  if ((frames >= (2U * VP37_CURRENT_SUPPLY_MIN_PER_PHASE)) &&
      (frames <= block->frames)) {
    const uint32_t first = block->frames - frames;
    uint64_t sum = 0U;
    bool valid = true;
    for (uint32_t k = first; k < block->frames; k++) {
      const uint16_t raw =
          VP37_currentBlockRaw(block, k, block->supplyPosition, compensated);
      if ((raw == 0U) || (raw >= VP37_CURRENT_ADC_MAX_RAW)) {
        valid = false;
      }
      sum += raw;
    }
    if (valid) {
      const uint64_t roundedMean = (sum + ((uint64_t)frames / 2U)) / frames;
      const int mean = (int)roundedMean;
      out->supplyLatestValid =
          fiesta_adc_to_voltage_ex(mean, (float)V_DIVIDER_R1,
                                   (float)V_DIVIDER_R2,
                                   &out->supplyLatestVolts) == HAL_OK;
      out->supplyLatestUs =
          block->startUs + VP37_currentFramesToUs(first, block->frameNs) +
          (VP37_currentFramesToUs(frames, block->frameNs) / 2U);
    }
  }
}

hal_status_t VP37_currentScanReduce(const VP37CurrentScanBlock *block,
                                    VP37CurrentPulseResult *out) {
  const hal_status_t status =
      VP37_currentScanReducePeriod(block, out, NULL, 0U, false);
  if (status != HAL_EINVAL) {
    VP37_currentReduceLatestSupply(block, out, false);
  }
  return status;
}

static void VP37_currentRetainScanBlock(const hal_adc_scan_block_t *block,
                                        uint32_t frameNs) {
  if (s_scanHistoryFrames != 0U) {
    const uint32_t interval = block->completed_us - s_scanHistoryCompletedUs;
    const uint32_t expected = VP37_currentFramesToUs(block->frames, frameNs);
    const uint32_t difference =
        (interval >= expected) ? (interval - expected) : (expected - interval);
    if ((block->sequence != (s_scanHistorySequence + 1U)) ||
        (frameNs != s_scanHistoryFrameNs) ||
        (difference > VP37_CURRENT_SCAN_TIMESTAMP_TOLERANCE_US)) {
      s_scanHistoryFrames = 0U;
    }
  }
  const uint32_t available = VP37_CURRENT_SCAN_HISTORY_FRAMES - block->frames;
  const uint32_t keep =
      (s_scanHistoryFrames < available) ? s_scanHistoryFrames : available;
  if (s_scanHistoryFrames == 0U) {
    (void)memset(&s_scanEdges, 0, sizeof(s_scanEdges));
    s_scanPulseCached = false;
    s_scanFirstFrame = 0U;
    s_scanHistoryStartUs =
        block->completed_us - VP37_currentFramesToUs(block->frames, frameNs);
  } else {
    s_scanFirstFrame += s_scanHistoryFrames - keep;
    s_scanHistoryStartUs +=
        VP37_currentFramesToUs(s_scanHistoryFrames - keep, frameNs);
  }
  const uint32_t pins = block->pin_count;
  (void)memmove(s_scanHistory,
                &s_scanHistory[(s_scanHistoryFrames - keep) * pins],
                keep * pins * sizeof(s_scanHistory[0]));
  (void)memcpy(&s_scanHistory[keep * pins], block->samples,
               block->frames * pins * sizeof(s_scanHistory[0]));
  // Compensate each arriving conversion once, before any overlapping window.
  for (uint32_t k = keep; k < keep + block->frames; ++k) {
    uint16_t *frame = &s_scanHistory[k * pins];
    frame[s_scanShuntPosition] =
        VP37_currentCompensatedRaw((int)frame[s_scanShuntPosition]);
    frame[s_scanSupplyPosition] =
        VP37_currentCompensatedRaw((int)frame[s_scanSupplyPosition]);
  }
  const VP37CurrentScanBlock newFrames = {
      .samples = &s_scanHistory[keep * pins],
      .frames = block->frames,
      .pinCount = block->pin_count,
      .shuntPosition = s_scanShuntPosition,
      .supplyPosition = s_scanSupplyPosition,
      .frameNs = frameNs,
      .startUs = 0U};
  VP37_currentTrackEdges(&newFrames, s_scanFirstFrame + keep, &s_scanEdges,
                         true);
  s_scanHistoryFrames = keep + block->frames;
  s_scanHistorySequence = block->sequence;
  s_scanHistoryCompletedUs = block->completed_us;
  s_scanHistoryFrameNs = frameNs;
}

hal_status_t VP37_currentScanPoll(uint32_t *sequence) {
  if (sequence == NULL) {
    return HAL_EINVAL;
  }
  *sequence = 0U;
  hal_adc_scan_block_t block;
  const hal_status_t takeStatus = hal_adc_scan_take(&block);
  if (takeStatus != HAL_OK) {
    if (takeStatus != HAL_EAGAIN) {
      s_scanHistoryFrames = 0U;
      s_scanPending = false;
    }
    return takeStatus;
  }
  const uint32_t frameNs = hal_adc_scan_frame_period_ns();
  hal_status_t status = HAL_ESTATE;
  if ((block.pin_count != VP37_CURRENT_SCAN_PINS) ||
      (block.frames > VP37_CURRENT_SCAN_HISTORY_FRAMES) || (frameNs == 0U)) {
    s_scanHistoryFrames = 0U;
    s_scanPending = false;
  } else {
    const uint32_t startedUs = hal_micros();
    VP37_currentRetainScanBlock(&block, frameNs);
    s_scanHistoryCollectedUs = hal_micros();
    s_scanHistoryPollUs = s_scanHistoryCollectedUs - startedUs;
    s_scanPending = true;
    *sequence = block.sequence;
    status = HAL_OK;
  }
  return status;
}

hal_status_t VP37_currentScanCollect(VP37CurrentPulseResult *out,
                                     uint32_t *sequence) {
  if ((out == NULL) || (sequence == NULL)) {
    return HAL_EINVAL;
  }
  *sequence = 0U;
  if (!hal_adc_scan_is_running()) {
    return HAL_ESTATE;
  }
  if (!s_scanPending) {
    return HAL_EAGAIN;
  }
  const VP37CurrentScanBlock view = {.samples = s_scanHistory,
                                     .frames = s_scanHistoryFrames,
                                     .pinCount = VP37_CURRENT_SCAN_PINS,
                                     .shuntPosition = s_scanShuntPosition,
                                     .supplyPosition = s_scanSupplyPosition,
                                     .frameNs = s_scanHistoryFrameNs,
                                     .startUs = s_scanHistoryStartUs};
  *sequence = s_scanHistorySequence;
  s_scanPending = false;
  hal_status_t reduced = HAL_OK;
  if (s_scanPulseCached && s_scanEdges.found &&
      (s_scanPulseFall == s_scanEdges.periodFall) &&
      ((s_scanEdges.periodLatch - s_scanFirstFrame) < s_scanHistoryFrames)) {
    // A completed pulse is immutable; only the independent supply advances.
    *out = s_scanPulse;
    out->glitches = s_scanEdges.glitches;
  } else {
    reduced = VP37_currentScanReducePeriod(&view, out, &s_scanEdges,
                                           s_scanFirstFrame, true);
    s_scanPulseCached = reduced == HAL_OK;
    if (s_scanPulseCached) {
      s_scanPulse = *out;
      s_scanPulseFall = s_scanEdges.periodFall;
    }
  }
  if (reduced != HAL_EINVAL) {
    VP37_currentReduceLatestSupply(&view, out, true);
  }
  out->scanCompletedUs = s_scanHistoryCompletedUs;
  out->scanCollectedUs = s_scanHistoryCollectedUs;
  out->scanPollUs = s_scanHistoryPollUs;
  return reduced;
}
