#include "../../common/fiesta_sensor_helpers.h"
#include "config.h"
#include "hal/impl/.mock/hal_mock.h"
#include "hardwareConfig.h"
#include "unity.h"
#include "vp37_current.h"

void setUp(void) {
  hal_mock_set_micros(0U);
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 0);
  VP37_currentSenseInit();
}

void tearDown(void) { (void)VP37_currentScanStop(); }

static uint16_t ampsToRaw(float amps) {
  return (uint16_t)((amps * VP37_CURRENT_SHUNT_OHMS *
                     (float)VP37_CURRENT_ADC_MAX_RAW / 3.3f) +
                    0.5f);
}

void test_pulse_analyze_guards_edges_and_winsorizes_a_switching_spike(void) {
  VP37CurrentPhaseSample samples[48] = {};
  const uint32_t cycleStartUs = 1000U;
  const uint32_t periodUs = 1000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ;
  const uint32_t sampleStepUs = periodUs / 160U;
  const uint32_t onTimeUs = sampleStepUs * COUNTOF(samples);
  for (uint32_t i = 0U; i < COUNTOF(samples); i++) {
    const float fraction = (float)i / (float)(COUNTOF(samples) - 1U);
    float amps = 3.0f + (2.0f * fraction);
    if ((i == 0U) || (i == 24U) || (i == COUNTOF(samples) - 1U)) {
      amps = 10.0f;
    }
    samples[i].timestampUs = cycleStartUs + (i * sampleStepUs);
    samples[i].rawSample = ampsToRaw(amps);
    samples[i].gateOn = 1U;
  }

  VP37CurrentPulseResult result;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_currentPulseAnalyze(samples, COUNTOF(samples), cycleStartUs,
                                       onTimeUs, periodUs, &result));
  TEST_ASSERT_TRUE(result.waveformValid);
  TEST_ASSERT_TRUE(result.guardedSamples < result.samples);
  TEST_ASSERT_GREATER_THAN_FLOAT(9.0f, result.peakAmps);
  TEST_ASSERT_LESS_THAN_FLOAT(5.2f, result.p95Amps);
  TEST_ASSERT_FLOAT_WITHIN(0.25f, 4.0f, result.meanAmps);
  const int32_t expectedPwm =
      (int32_t)(((uint64_t)onTimeUs * PWM_RESOLUTION) / periodUs);
  TEST_ASSERT_INT32_WITHIN(2, expectedPwm, result.pwmCommand);
}

void test_pulse_analyze_rejects_bad_timing_and_clipping(void) {
  VP37CurrentPhaseSample samples[16] = {};
  const uint32_t periodUs = 1000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ;
  const uint32_t onTimeUs = periodUs / 4U;
  for (uint32_t i = 0U; i < COUNTOF(samples); i++) {
    samples[i].timestampUs =
        1000U + 60U + (i * ((onTimeUs - 120U) / COUNTOF(samples)));
    samples[i].rawSample = ampsToRaw(4.0f);
    samples[i].gateOn = 1U;
  }

  VP37CurrentPulseResult result;
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, VP37_currentPulseAnalyze(nullptr, COUNTOF(samples), 1000U,
                                           onTimeUs, periodUs, &result));
  TEST_ASSERT_EQUAL_INT(
      HAL_EINVAL, VP37_currentPulseAnalyze(samples, COUNTOF(samples), 1000U,
                                           periodUs, periodUs, &result));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentPulseAnalyze(
                                    samples, COUNTOF(samples), 1000U, onTimeUs,
                                    (periodUs * 14U) / 10U, &result));
  TEST_ASSERT_FALSE(result.waveformValid);

  samples[8].clipped = 1U;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_currentPulseAnalyze(samples, COUNTOF(samples), 1000U,
                                       onTimeUs, periodUs, &result));
  TEST_ASSERT_EQUAL_UINT32(1U, result.clippedSamples);
  TEST_ASSERT_FALSE(result.waveformValid);
}

void test_pulse_analyze_handles_wrap_and_rejects_samples_outside_cycle(void) {
  VP37CurrentPhaseSample samples[12] = {};
  const uint32_t periodUs = 1000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ;
  const uint32_t onTimeUs = periodUs / 4U;
  const uint32_t sampleStepUs = (onTimeUs - 120U) / COUNTOF(samples);
  const uint32_t start = UINT32_MAX - 200U;
  for (uint32_t i = 0U; i < COUNTOF(samples); i++) {
    samples[i] = {start + 60U + i * sampleStepUs, ampsToRaw(6.5f), 1U, 0U};
  }
  samples[0].timestampUs = start - 1U;
  samples[1].timestampUs = start + onTimeUs + 1U;
  VP37CurrentPulseResult result;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, VP37_currentPulseAnalyze(samples, COUNTOF(samples), start,
                                       onTimeUs, periodUs, &result));
  TEST_ASSERT_EQUAL_UINT32(10U, result.guardedSamples);
  TEST_ASSERT_TRUE(result.waveformValid);
  // High current is an observation, without an arbitrary cutoff threshold.
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.5f, result.meanAmps);
  TEST_ASSERT_EQUAL_INT(
      HAL_EOVERFLOW,
      VP37_currentPulseAnalyze(samples, VP37_CURRENT_PULSE_SAMPLES + 1U, start,
                               onTimeUs, periodUs, &result));
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        VP37_currentPulseAnalyze(samples, 8U, start, onTimeUs,
                                                 periodUs, &result));
}

void test_pulse_profile_preserves_time_order_and_rejects_invalid_waveforms(
    void) {
  VP37CurrentPhaseSample samples[64] = {};
  const uint32_t period = 1000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ;
  const uint32_t on = period / 2U;
  const uint32_t start = UINT32_MAX - on / 2U;
  for (uint32_t i = 0U; i < COUNTOF(samples); ++i) {
    const float amps = 2.0f + .3f * (float)(i / 8U);
    samples[i] = {start + 60U + i * (on - 120U) / 63U, ampsToRaw(amps), 1U, 0U};
  }
  VP37CurrentPulseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_currentPulseAnalyze(samples, COUNTOF(samples),
                                                 start, on, period, &result));
  TEST_ASSERT_TRUE(result.profileValid);
  for (uint32_t bin = 0U; bin < VP37_CURRENT_PROFILE_BINS; ++bin) {
    TEST_ASSERT_FLOAT_WITHIN(.005f, 2.0f + .3f * (float)bin,
                             result.profileAmps[bin]);
    uint32_t sum = 0U;
    for (uint32_t j = 0U; j < 8U; ++j) {
      sum += samples[bin * 8U + j].timestampUs - start;
    }
    TEST_ASSERT_EQUAL_UINT32((sum + 4U) / 8U, result.profileUs[bin]);
  }
  samples[20].clipped = 1U;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        VP37_currentPulseAnalyze(samples, COUNTOF(samples),
                                                 start, on, period, &result));
  TEST_ASSERT_FALSE(result.profileValid);
  samples[20].clipped = 0U;
  // Enough guarded samples for a mean, but only the early bins are populated.
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentPulseAnalyze(samples, 16U, start,
                                                         on, period, &result));
  TEST_ASSERT_TRUE(result.waveformValid);
  TEST_ASSERT_FALSE(result.profileValid);
}

// Frame layout of the ECU scan on RP: shunt, sensor mux, supply.
static constexpr uint8_t kScanPins = VP37_CURRENT_SCAN_PINS;
static constexpr uint8_t kShuntPosition = 0U;
static constexpr uint8_t kSupplyPosition = 2U;
static constexpr uint32_t kFrameNs = VP37_CURRENT_SCAN_FRAME_NS;
static constexpr uint32_t kBlockFrames = VP37_CURRENT_SCAN_HISTORY_FRAMES;
static constexpr uint32_t kDmaFrames = VP37_CURRENT_SCAN_BLOCK_FRAMES;
static constexpr uint32_t kPeriodFrames =
    (1000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ) * 1000U / kFrameNs;
static constexpr uint32_t kOnFrames = (kPeriodFrames * 2U) / 5U; // 40 %
static uint16_t s_block[kBlockFrames * kScanPins];

struct Waveform {
  uint32_t offsetFrames; /**< Frame of the first rising edge. */
  uint16_t zeroRaw;
  uint16_t supplyOn;
  uint16_t supplyOff;
  bool edges;
};

static void fillFrames(const Waveform &w, uint32_t frames, uint32_t firstFrame,
                       uint32_t periodFrames = kPeriodFrames) {
  const uint32_t onFrames = (periodFrames * 2U) / 5U;
  for (uint32_t k = 0U; k < frames; k++) {
    const uint32_t phase =
        (k + firstFrame + periodFrames - (w.offsetFrames % periodFrames)) %
        periodFrames;
    const bool on = w.edges && (phase < onFrames);
    uint16_t shunt = w.zeroRaw;
    if (on) {
      const float amps = 3.0f + (2.0f * (float)phase / (float)(onFrames - 1U));
      shunt = (uint16_t)(w.zeroRaw + ampsToRaw(amps));
    }
    s_block[(k * kScanPins) + kShuntPosition] = shunt;
    s_block[(k * kScanPins) + 1U] = 1234U;
    s_block[(k * kScanPins) + kSupplyPosition] = on ? w.supplyOn : w.supplyOff;
  }
}

static void fillBlock(const Waveform &w) { fillFrames(w, kBlockFrames, 0U); }

static VP37CurrentScanBlock blockView(uint32_t startUs) {
  VP37CurrentScanBlock view;
  view.samples = s_block;
  view.frames = kBlockFrames;
  view.pinCount = kScanPins;
  view.shuntPosition = kShuntPosition;
  view.supplyPosition = kSupplyPosition;
  view.frameNs = kFrameNs;
  view.startUs = startUs;
  return view;
}

static uint32_t framesToUs(uint32_t frames, uint32_t frameNs = kFrameNs) {
  return (uint32_t)((((uint64_t)frames * frameNs) + 500U) / 1000U);
}

/* A complete ON phase and its preceding fall must both be inside history. */
static uint32_t newestPeriodStart(uint32_t offset,
                                  uint32_t frames = kBlockFrames) {
  uint32_t best = UINT32_MAX;
  for (uint32_t rise = offset;
       rise + kOnFrames + VP37_CURRENT_GATE_CONFIRM_FRAMES <= frames;
       rise += kPeriodFrames) {
    if (rise + kOnFrames > kPeriodFrames) {
      best = rise;
    }
  }
  return best;
}

void test_scan_reduce_rejects_bad_view_zero_and_blocks_without_edges(void) {
  VP37CurrentPulseResult result;
  fillBlock({0U, 16U, 3000U, 3100U, true});
  VP37CurrentScanBlock view = blockView(1000U);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentScanReduce(nullptr, &result));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentScanReduce(&view, nullptr));
  view.shuntPosition = kScanPins;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentScanReduce(&view, &result));
  view = blockView(1000U);
  view.frameNs = 0U;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentScanReduce(&view, &result));

  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 160);
  VP37_currentSenseInit();
  view = blockView(1000U);
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_FALSE(result.zeroValid);
  TEST_ASSERT_EQUAL_UINT16(160U, result.zeroRaw);

  // Without gate edges the current and its paired supply remain unavailable;
  // the latest supply window is still usable.
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 16);
  VP37_currentSenseInit();
  fillBlock({0U, 16U, 3000U, 3100U, false});
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_TRUE(result.zeroValid);
  TEST_ASSERT_EQUAL_UINT16(16U, result.zeroRaw);
  TEST_ASSERT_FALSE(result.waveformValid);
  TEST_ASSERT_FALSE(result.supplyValid);
  TEST_ASSERT_TRUE(result.supplyLatestValid);
  TEST_ASSERT_EQUAL_UINT32(0U, result.periodUs);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, result.meanAmps);
}

void test_scan_reduce_measures_the_newest_full_period(void) {
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 16);
  VP37_currentSenseInit();
  // Rising edges every period from frame 0: the block starts inside an ON
  // phase, and the newest period whose closing edge is confirmed inside the
  // block is the one reported, whatever the block length in periods.
  fillBlock({0U, 16U, 3000U, 3100U, true});
  const uint32_t startUs = 123456U;
  const VP37CurrentScanBlock view = blockView(startUs);
  VP37CurrentPulseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_TRUE(result.zeroValid);
  TEST_ASSERT_TRUE(result.waveformValid);
  const uint32_t newest = newestPeriodStart(0U);
  TEST_ASSERT_TRUE(newest >= kPeriodFrames);
  TEST_ASSERT_TRUE(newest + kPeriodFrames + kOnFrames +
                       VP37_CURRENT_GATE_CONFIRM_FRAMES >
                   kBlockFrames);
  TEST_ASSERT_EQUAL_UINT32(startUs + framesToUs(newest), result.cycleStartUs);
  TEST_ASSERT_EQUAL_UINT32(framesToUs(kPeriodFrames), result.periodUs);
  TEST_ASSERT_EQUAL_UINT32(framesToUs(kOnFrames), result.onTimeUs);
  TEST_ASSERT_EQUAL_UINT32(kOnFrames, result.samples);
  TEST_ASSERT_EQUAL_UINT32(0U, result.clippedSamples);
  const int32_t expectedPwm =
      (int32_t)(((uint64_t)result.onTimeUs * PWM_RESOLUTION +
                 (result.periodUs / 2U)) /
                result.periodUs);
  TEST_ASSERT_EQUAL_INT32(expectedPwm, result.pwmCommand);
  // Guards cut both ends of the 3..5 A ramp symmetrically; the P95 winsor
  // trims the top a little.
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 4.0f, result.meanAmps);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, result.peakAmps);
  TEST_ASSERT_TRUE(result.p95Amps <= result.peakAmps);
  // The supply is weighted by the measured duty: 0.4 * 3000 + 0.6 * 3100.
  TEST_ASSERT_TRUE(result.supplyValid);
  TEST_ASSERT_EQUAL_UINT32(kPeriodFrames, result.supplySamples);
  float expectedVolts = 0.0f;
  TEST_ASSERT_EQUAL_INT(
      HAL_OK, fiesta_adc_to_voltage_ex(3060, (float)V_DIVIDER_R1,
                                       (float)V_DIVIDER_R2, &expectedVolts));
  TEST_ASSERT_FLOAT_WITHIN(0.15f, expectedVolts, result.supplyVolts);

  // Any phase of the same waveform leaves a complete period in the block and
  // the newest one is reported.
  for (uint32_t offset = 1U; offset < kPeriodFrames; offset += 7U) {
    fillBlock({offset, 16U, 3000U, 3100U, true});
    TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
    TEST_ASSERT_NOT_EQUAL_UINT32(UINT32_MAX, newestPeriodStart(offset));
    TEST_ASSERT_EQUAL_UINT32(startUs + framesToUs(newestPeriodStart(offset)),
                             result.cycleStartUs);
    TEST_ASSERT_EQUAL_UINT32(framesToUs(kPeriodFrames), result.periodUs);
    TEST_ASSERT_TRUE(result.waveformValid);
  }
}

void test_scan_reduce_keeps_supply_and_current_validity_separate(void) {
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 16);
  VP37_currentSenseInit();
  VP37CurrentPulseResult result;
  const VP37CurrentScanBlock view = blockView(UINT32_MAX - 1000U);

  // One clipped shunt sample inside the newest period rejects the waveform,
  // not the supply mean; the wrapped start is handled like any other.
  const uint32_t newest = newestPeriodStart(0U);
  fillBlock({0U, 16U, 3000U, 3100U, true});
  s_block[((newest + (kOnFrames / 2U)) * kScanPins) + kShuntPosition] =
      VP37_CURRENT_ADC_MAX_RAW;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_EQUAL_UINT32(1U, result.clippedSamples);
  TEST_ASSERT_FALSE(result.waveformValid);
  TEST_ASSERT_TRUE(result.supplyValid);
  TEST_ASSERT_EQUAL_UINT32((UINT32_MAX - 1000U) + framesToUs(newest),
                           result.cycleStartUs);

  // A supply conversion at the end stop rejects the supply mean only.
  fillBlock({0U, 16U, 3000U, 3100U, true});
  s_block[((newest - 1U) * kScanPins) + kSupplyPosition] =
      VP37_CURRENT_ADC_MAX_RAW;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_TRUE(result.waveformValid);
  TEST_ASSERT_FALSE(result.supplyValid);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, result.supplyVolts);
}

void test_scan_latch_tracks_falling_edges_when_duty_changes(void) {
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 16);
  VP37_currentSenseInit();
  const uint32_t previousFall = 10U;
  const uint32_t firstFall = previousFall + kPeriodFrames;
  const uint32_t secondFall = firstFall + kPeriodFrames;
  const uint32_t firstOn = (kPeriodFrames * 3U) / 10U;
  const uint32_t secondOn = (kPeriodFrames * 4U) / 10U;
  const uint32_t firstRise = firstFall - firstOn;
  const uint32_t secondRise = secondFall - secondOn;
  for (uint32_t k = 0U; k < kBlockFrames; ++k) {
    // Physical ON is late in each cycle. Raising duty advances the next ON
    // rise, while hardware wrap and the two falling edges stay one period
    // apart.
    const bool on = (k < previousFall) ||
                    ((k >= firstRise) && (k < firstFall)) ||
                    ((k >= secondRise) && (k < secondFall));
    s_block[k * kScanPins] = (uint16_t)(16U + (on ? ampsToRaw(4.0f) : 0U));
    s_block[k * kScanPins + 1U] = 1234U;
    s_block[k * kScanPins + 2U] = on ? 3000U : 3100U;
  }
  const uint32_t start = UINT32_MAX - 1000U;
  VP37CurrentScanBlock view = blockView(start);
  // Stop just before the first fall has enough confirmation samples.
  view.frames = firstFall + VP37_CURRENT_GATE_CONFIRM_FRAMES - 1U;
  VP37CurrentPulseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_FALSE(result.waveformValid);
  view.frames++;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_TRUE(result.waveformValid);
  TEST_ASSERT_TRUE(result.supplyValid);
  TEST_ASSERT_TRUE(result.latchValid);
  TEST_ASSERT_EQUAL_UINT32(start + framesToUs(previousFall), result.latchUs);
  TEST_ASSERT_EQUAL_UINT32(framesToUs(kPeriodFrames), result.latchPeriodUs);
  TEST_ASSERT_EQUAL_UINT32(start + framesToUs(firstRise), result.cycleStartUs);
  TEST_ASSERT_EQUAL_UINT32(result.latchPeriodUs, result.periodUs);
  const int32_t expected =
      (int32_t)(((uint64_t)firstOn * PWM_RESOLUTION + kPeriodFrames / 2U) /
                kPeriodFrames);
  TEST_ASSERT_EQUAL_INT32(expected, result.latchedPwm);
  TEST_ASSERT_EQUAL_INT32(result.latchedPwm, result.pwmCommand);

  // Without the preceding fall, neither period nor command is known yet.
  const uint32_t removed = previousFall + 1U;
  view.samples += removed * kScanPins;
  view.frames -= removed;
  view.startUs += framesToUs(removed);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_FALSE(result.waveformValid);
  TEST_ASSERT_FALSE(result.supplyValid);
  TEST_ASSERT_FALSE(result.latchValid);

  // The next completed ON phase replaces the older one despite changing duty.
  view = blockView(start);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_TRUE(result.waveformValid);
  TEST_ASSERT_TRUE(result.latchValid);
  TEST_ASSERT_EQUAL_UINT32(start + framesToUs(firstFall), result.latchUs);
  TEST_ASSERT_EQUAL_UINT32(start + framesToUs(secondRise), result.cycleStartUs);
  TEST_ASSERT_EQUAL_UINT32(framesToUs(kPeriodFrames), result.periodUs);
  const uint32_t secondOnUs = framesToUs(secondOn);
  const uint32_t periodUs = framesToUs(kPeriodFrames);
  TEST_ASSERT_EQUAL_INT32(
      (int32_t)(((uint64_t)secondOnUs * PWM_RESOLUTION + periodUs / 2U) /
                periodUs),
      result.latchedPwm);
}

void test_scan_reduce_ignores_single_frame_spikes_and_dropouts(void) {
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 16);
  VP37_currentSenseInit();
  VP37CurrentPulseResult result;
  const uint32_t startUs = 5000U;
  const VP37CurrentScanBlock view = blockView(startUs);
  // A one-frame excursion in the OFF phase right before the newest rising
  // edge is not a gate; the real period before it is reported.
  fillBlock({0U, 16U, 3000U, 3100U, true});
  const uint32_t newest = newestPeriodStart(0U);
  s_block[((newest - 5U) * kScanPins) + kShuntPosition] =
      (uint16_t)(16U + ampsToRaw(4.0f));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_TRUE(result.waveformValid);
  TEST_ASSERT_EQUAL_UINT32(framesToUs(kOnFrames), result.onTimeUs);
  TEST_ASSERT_EQUAL_UINT32(framesToUs(kPeriodFrames), result.periodUs);
  TEST_ASSERT_EQUAL_UINT32(1U, result.glitches);
  // A dropout shorter than the confirmation window inside the ON phase does
  // not split the phase either.
  fillBlock({0U, 16U, 3000U, 3100U, true});
  for (uint32_t k = 0U; k < VP37_CURRENT_GATE_CONFIRM_FRAMES - 1U; k++) {
    s_block[((newest + (kOnFrames / 2U) + k) * kScanPins) + kShuntPosition] =
        16U;
  }
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_TRUE(result.waveformValid);
  TEST_ASSERT_EQUAL_UINT32(framesToUs(kOnFrames), result.onTimeUs);
  TEST_ASSERT_EQUAL_UINT32(startUs + framesToUs(newest), result.cycleStartUs);
  TEST_ASSERT_EQUAL_UINT32(1U, result.glitches);
  // A clean block reports none.
  fillBlock({0U, 16U, 3000U, 3100U, true});
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_EQUAL_UINT32(0U, result.glitches);
}

static hal_status_t pollAndCollect(VP37CurrentPulseResult *result,
                                   uint32_t *sequence) {
  uint32_t taken = 0U;
  (void)VP37_currentScanPoll(&taken);
  return VP37_currentScanCollect(result, sequence);
}

static void fillTransferGapFrames(uint16_t raw, uint32_t frames,
                                  uint32_t firstFrame) {
  fillFrames({3U, 16U, raw, raw, true}, frames, firstFrame);
  for (uint32_t k = 0U; k < frames; ++k) {
    uint16_t &shunt = s_block[k * kScanPins + kShuntPosition];
    if (shunt > 16U) {
      shunt = raw;
    }
  }
}

void test_streaming_compensation_matches_raw_reduction_at_transfer_gaps(void) {
  const uint16_t levels[] = {0U,    509U,  510U,  511U,  512U,  513U,  1534U,
                             1535U, 1536U, 1537U, 2558U, 2559U, 2560U, 2561U,
                             3582U, 3583U, 3584U, 3585U, 4095U};
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 16);
  VP37_currentSenseInit();
  const uint32_t start = UINT32_MAX - 12000U;
  const uint32_t blocks = (kBlockFrames + kDmaFrames - 1U) / kDmaFrames + 3U;
  for (size_t trial = 0U; trial < COUNTOF(levels); ++trial) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStart());
    for (uint32_t i = 0U; i < blocks; ++i) {
      fillTransferGapFrames(levels[trial], kDmaFrames, i * kDmaFrames);
      const uint32_t total = (i + 1U) * kDmaFrames;
      hal_mock_set_micros(start + framesToUs(total));
      TEST_ASSERT_EQUAL_INT(HAL_OK,
                            hal_mock_adc_scan_complete(s_block, kDmaFrames));
      VP37CurrentPulseResult actual;
      uint32_t sequence = 0U;
      const hal_status_t status = pollAndCollect(&actual, &sequence);

      const uint32_t retained = total < kBlockFrames ? total : kBlockFrames;
      fillTransferGapFrames(levels[trial], retained, total - retained);
      VP37CurrentScanBlock view =
          blockView(start + framesToUs(total - retained));
      view.frames = retained;
      VP37CurrentPulseResult expected;
      TEST_ASSERT_EQUAL_INT(VP37_currentScanReduce(&view, &expected), status);
      actual.scanCompletedUs = 0U;
      actual.scanCollectedUs = 0U;
      actual.scanPollUs = 0U;
      TEST_ASSERT_EQUAL_MEMORY(&expected, &actual, sizeof(expected));
    }
    TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStop());
  }
}

void test_scan_collect_takes_each_mock_block_once(void) {
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 16);
  VP37_currentSenseInit();
  VP37CurrentPulseResult result;
  uint32_t sequence = 99U;
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL,
                        VP37_currentScanCollect(nullptr, &sequence));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentScanCollect(&result, nullptr));
  TEST_ASSERT_EQUAL_UINT32(0U, VP37_currentScanFrameNs());
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE,
                        VP37_currentScanCollect(&result, &sequence));
  TEST_ASSERT_EQUAL_UINT32(0U, sequence);

  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStart());
  TEST_ASSERT_EQUAL_UINT32(kFrameNs, VP37_currentScanFrameNs());
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        VP37_currentScanCollect(&result, &sequence));
  TEST_ASSERT_EQUAL_UINT32(0U, sequence);

  const uint32_t startUs = 500000U;
  const uint32_t blocks = (kBlockFrames + kDmaFrames - 1U) / kDmaFrames;
  for (uint32_t i = 0U; i < blocks; i++) {
    fillFrames({3U, 16U, 3000U, 3100U, true}, kDmaFrames, i * kDmaFrames);
    hal_mock_set_micros(startUs + framesToUs((i + 1U) * kDmaFrames));
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_mock_adc_scan_complete(s_block, kDmaFrames));
    const hal_status_t status = pollAndCollect(&result, &sequence);
    TEST_ASSERT_TRUE((status == HAL_OK) || (status == HAL_EAGAIN));
    TEST_ASSERT_EQUAL_UINT32(i + 1U, sequence);
  }
  TEST_ASSERT_TRUE(result.waveformValid);
  const uint32_t total = blocks * kDmaFrames;
  TEST_ASSERT_EQUAL_UINT32(startUs + framesToUs(newestPeriodStart(3U, total)),
                           result.cycleStartUs);
  // A block is handed out once.
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        VP37_currentScanCollect(&result, &sequence));
  TEST_ASSERT_EQUAL_UINT32(0U, sequence);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStop());
  TEST_ASSERT_EQUAL_UINT32(0U, VP37_currentScanFrameNs());
}

static float voltsForCompensatedRaw(int raw) {
  float volts = 0.0f;
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        fiesta_adc_to_voltage_ex(raw, (float)V_DIVIDER_R1,
                                                 (float)V_DIVIDER_R2, &volts));
  return volts;
}

void test_scan_poll_retains_fast_blocks_between_reductions(void) {
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, VP37_currentScanPoll(nullptr));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStart());
  const uint32_t start = UINT32_MAX - 12000U;
  uint32_t received = 0U;
  for (uint32_t i = 0U; i < 24U; ++i) {
    fillFrames({3U, 0U, 3000U, 3100U, true}, kDmaFrames, i * kDmaFrames);
    const uint32_t completed = start + framesToUs((i + 1U) * kDmaFrames);
    hal_mock_set_micros(completed);
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          hal_mock_adc_scan_complete(s_block, kDmaFrames));
    hal_mock_set_micros(completed + 80U);
    TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanPoll(&received));
    TEST_ASSERT_EQUAL_UINT32(i + 1U, received);
    TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, VP37_currentScanPoll(&received));
    TEST_ASSERT_EQUAL_UINT32(0U, received);
    if (((i + 1U) % 3U) == 0U) {
      VP37CurrentPulseResult result;
      uint32_t sequence = 0U;
      const hal_status_t status = VP37_currentScanCollect(&result, &sequence);
      TEST_ASSERT_TRUE((status == HAL_OK) || (status == HAL_EAGAIN));
      TEST_ASSERT_EQUAL_UINT32(i + 1U, sequence);
      TEST_ASSERT_EQUAL_UINT32(completed, result.scanCompletedUs);
      TEST_ASSERT_EQUAL_UINT32(completed + 80U, result.scanCollectedUs);
      if (i >= 11U) {
        TEST_ASSERT_TRUE(result.waveformValid);
        const uint32_t newest = newestPeriodStart(3U, (i + 1U) * kDmaFrames);
        TEST_ASSERT_EQUAL_UINT32(start + framesToUs(newest),
                                 result.cycleStartUs);
      }
      TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                            VP37_currentScanCollect(&result, &sequence));
      TEST_ASSERT_EQUAL_UINT32(0U, sequence);
    }
  }
}

static uint32_t nominalPeriodFrames(void) {
  return ((1000000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ) + (kFrameNs / 2U)) /
         kFrameNs;
}

void test_scan_reduce_preserves_integer_fractional_and_wrapped_timestamps(
    void) {
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 16);
  VP37_currentSenseInit();
  const uint32_t framePeriods[] = {kFrameNs, kFrameNs + 333U};
  const uint32_t offsets[] = {0U, 3U, 7U};
  for (size_t timing = 0U; timing < COUNTOF(framePeriods); ++timing) {
    const uint32_t frameNs = framePeriods[timing];
    for (size_t phase = 0U; phase < COUNTOF(offsets); ++phase) {
      fillBlock({offsets[phase], 16U, 3000U, 3000U, true});
      const uint32_t newest = newestPeriodStart(offsets[phase]);
      const uint32_t onUs = framesToUs(kOnFrames, frameNs);
      const uint32_t starts[] = {
          123456U, UINT32_MAX - framesToUs(newest, frameNs) - (onUs / 2U)};
      for (size_t origin = 0U; origin < COUNTOF(starts); ++origin) {
        VP37CurrentScanBlock view = blockView(starts[origin]);
        view.frameNs = frameNs;
        VP37CurrentPhaseSample samples[kOnFrames] = {};
        for (uint32_t i = 0U; i < kOnFrames; ++i) {
          const int raw =
              (int)s_block[((newest + i) * kScanPins) + kShuntPosition];
          // Reference timestamps keep the original rounded 64-bit formula.
          samples[i].timestampUs =
              view.startUs + framesToUs(newest + i, frameNs);
          samples[i].rawSample =
              (uint16_t)(hal_adc_compensate_rp2040_12bit(raw) - 16);
          samples[i].gateOn = 1U;
        }
        VP37CurrentPulseResult expected;
        TEST_ASSERT_EQUAL_INT(
            HAL_OK,
            VP37_currentPulseAnalyze(
                samples, kOnFrames, view.startUs + framesToUs(newest, frameNs),
                onUs, framesToUs(kPeriodFrames, frameNs), &expected));
        expected.latchUs =
            view.startUs +
            framesToUs(newest - kPeriodFrames + kOnFrames, frameNs);
        expected.latchPeriodUs = expected.periodUs;
        expected.latchedPwm = expected.pwmCommand;
        expected.latchValid = true;
        expected.supplySamples = kPeriodFrames;
        expected.supplyVolts = voltsForCompensatedRaw(3024);
        expected.supplyValid = true;
        const uint32_t latestFrames =
            (expected.periodUs * 1000U + frameNs / 2U) / frameNs;
        expected.supplyLatestVolts = expected.supplyVolts;
        expected.supplyLatestUs =
            view.startUs + framesToUs(kBlockFrames - latestFrames, frameNs) +
            framesToUs(latestFrames, frameNs) / 2U;
        expected.supplyLatestValid = true;
        VP37CurrentPulseResult actual;
        TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &actual));
        TEST_ASSERT_EQUAL_MEMORY(&expected, &actual, sizeof(expected));
      }
    }
  }
}

void test_latest_supply_tracks_the_end_of_history_and_its_center_timestamp(
    void) {
  // A ramp makes an old rise-aligned mean visibly different from the newest
  // window. Raw codes stay in one ADC compensation segment (+24).
  fillBlock({0U, 16U, 3000U, 3000U, true});
  for (uint32_t k = 0U; k < kBlockFrames; k++) {
    s_block[(k * kScanPins) + kSupplyPosition] = (uint16_t)(2800U + k / 2U);
  }
  const VP37CurrentScanBlock view = blockView(UINT32_MAX - 1000U);
  VP37CurrentPulseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_TRUE(result.supplyLatestValid);
  const uint32_t first = kBlockFrames - kPeriodFrames;
  const int centerRaw = 2824 + (int)((first + kBlockFrames - 1U) / 4U);
  TEST_ASSERT_FLOAT_WITHIN(.01f, voltsForCompensatedRaw(centerRaw),
                           result.supplyLatestVolts);
  TEST_ASSERT_EQUAL_UINT32(view.startUs + framesToUs(first) +
                               (framesToUs(kPeriodFrames) / 2U),
                           result.supplyLatestUs);
  TEST_ASSERT_GREATER_THAN_FLOAT(result.supplyVolts, result.supplyLatestVolts);

  // Old clipped samples cannot poison a newer, complete supply window.
  s_block[kSupplyPosition] = VP37_CURRENT_ADC_MAX_RAW;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_TRUE(result.supplyLatestValid);
  s_block[((kBlockFrames - 1U) * kScanPins) + kSupplyPosition] = 0U;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_FALSE(result.supplyLatestValid);
}

void test_latest_supply_uses_measured_period_and_rejects_phase_ripple(void) {
  // Ten percent frequency offset is plausible but makes the nominal window
  // measurably wrong for a large ON/OFF supply ripple.
  const uint32_t period = (kPeriodFrames * 11U) / 10U;
  const uint32_t on = (period * 2U) / 5U;
  const float meanRaw =
      (float)(on * 2824U + (period - on) * 3324U) / (float)period;
  const float expected = voltsForCompensatedRaw((int)(meanRaw + .5f));
  const VP37CurrentScanBlock view = blockView(1000U);
  for (uint32_t offset = 0U; offset < period; offset += 7U) {
    fillFrames({offset, 16U, 2800U, 3300U, true}, kBlockFrames, 0U, period);
    VP37CurrentPulseResult result;
    TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
    TEST_ASSERT_TRUE(result.supplyLatestValid);
    TEST_ASSERT_EQUAL_UINT32(framesToUs(period), result.periodUs);
    TEST_ASSERT_FLOAT_WITHIN(.006f, expected, result.supplyLatestVolts);
    TEST_ASSERT_EQUAL_UINT32(view.startUs + framesToUs(kBlockFrames - period) +
                                 (framesToUs(period) / 2U),
                             result.supplyLatestUs);
  }
}

void test_latest_supply_does_not_require_current_edges_or_shunt_zero(void) {
  fillBlock({0U, 16U, 3000U, 3000U, false});
  VP37CurrentScanBlock view = blockView(1000U);
  VP37CurrentPulseResult result;
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_TRUE(result.supplyLatestValid);
  TEST_ASSERT_FLOAT_WITHIN(.006f, voltsForCompensatedRaw(3024),
                           result.supplyLatestVolts);
  const uint32_t frames = nominalPeriodFrames();
  TEST_ASSERT_EQUAL_UINT32(view.startUs + framesToUs(kBlockFrames - frames) +
                               (framesToUs(frames) / 2U),
                           result.supplyLatestUs);

  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 160);
  VP37_currentSenseInit();
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_FALSE(result.zeroValid);
  TEST_ASSERT_TRUE(result.supplyLatestValid);
  view.frames = frames - 1U;
  TEST_ASSERT_EQUAL_INT(HAL_ESTATE, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_FALSE(result.supplyLatestValid);
}

static hal_status_t completeBlock(uint32_t completedUs,
                                  VP37CurrentPulseResult *result) {
  hal_mock_set_micros(completedUs);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_adc_scan_complete(s_block, kDmaFrames));
  uint32_t sequence = 0U;
  return pollAndCollect(result, &sequence);
}

void test_scan_history_publishes_fresh_supply_each_block_across_time_wrap(
    void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStart());
  const uint32_t start = UINT32_MAX - 15000U;
  const uint32_t periodFrames = nominalPeriodFrames();
  for (uint32_t i = 0U; i < 8U; i++) {
    fillFrames({0U, 0U, 3000U, 3000U, false}, kDmaFrames, i * kDmaFrames);
    const uint32_t total = (i + 1U) * kDmaFrames;
    VP37CurrentPulseResult result;
    TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                          completeBlock(start + framesToUs(total), &result));
    TEST_ASSERT_EQUAL(total >= periodFrames, result.supplyLatestValid);
    if (result.supplyLatestValid) {
      TEST_ASSERT_EQUAL_UINT32(start + framesToUs(total - periodFrames) +
                                   (framesToUs(periodFrames) / 2U),
                               result.supplyLatestUs);
      TEST_ASSERT_FLOAT_WITHIN(.006f, voltsForCompensatedRaw(3024),
                               result.supplyLatestVolts);
    }
  }
}

static uint32_t primeCurrentHistory(void) {
  uint32_t completedUs = 0U;
  const uint32_t blocks = (kBlockFrames + kDmaFrames - 1U) / kDmaFrames;
  VP37CurrentPulseResult result;
  for (uint32_t i = 0U; i < blocks; i++) {
    fillFrames({3U, 0U, 3000U, 3100U, true}, kDmaFrames, i * kDmaFrames);
    completedUs = framesToUs((i + 1U) * kDmaFrames);
    const hal_status_t status = completeBlock(completedUs, &result);
    TEST_ASSERT_TRUE((status == HAL_OK) || (status == HAL_EAGAIN));
  }
  TEST_ASSERT_TRUE(result.waveformValid);
  return completedUs;
}

static void assertFreshBlockAfterDiscontinuity(uint32_t completedUs) {
  VP37CurrentPulseResult result;
  fillFrames({0U, 0U, 3300U, 3300U, false}, kDmaFrames, 0U);
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, completeBlock(completedUs, &result));
  TEST_ASSERT_FALSE(result.waveformValid);
  TEST_ASSERT_EQUAL(kDmaFrames >= nominalPeriodFrames(),
                    result.supplyLatestValid);
  if (result.supplyLatestValid) {
    TEST_ASSERT_FLOAT_WITHIN(.006f, voltsForCompensatedRaw(3324),
                             result.supplyLatestVolts);
  }
  // Enough NEW contiguous blocks must replace the discarded history.
  const uint32_t blocks =
      (nominalPeriodFrames() + kDmaFrames - 1U) / kDmaFrames;
  for (uint32_t i = 1U; i < blocks; ++i) {
    TEST_ASSERT_EQUAL_INT(
        HAL_EAGAIN,
        completeBlock(completedUs + framesToUs(i * kDmaFrames), &result));
    TEST_ASSERT_EQUAL((i + 1U) * kDmaFrames >= nominalPeriodFrames(),
                      result.supplyLatestValid);
  }
  TEST_ASSERT_TRUE(result.supplyLatestValid);
  TEST_ASSERT_FLOAT_WITHIN(.006f, voltsForCompensatedRaw(3324),
                           result.supplyLatestVolts);
}

void test_scan_history_discards_missing_blocks_and_timestamp_gaps(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStart());
  uint32_t completed = primeCurrentHistory();
  // DMA completes a block without the consumer taking it: sequence skips.
  completed += framesToUs(kDmaFrames);
  hal_mock_set_micros(completed);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_adc_scan_complete(s_block, kDmaFrames));
  assertFreshBlockAfterDiscontinuity(completed + framesToUs(kDmaFrames));

  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStop());
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStart());
  completed = primeCurrentHistory();
  // Consecutive sequence, but a discontinuous time axis cannot be stitched.
  assertFreshBlockAfterDiscontinuity(completed + framesToUs(kDmaFrames) + 500U);
}

void test_scan_restart_discards_history_but_small_irq_jitter_preserves_it(
    void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStart());
  const uint32_t completed = primeCurrentHistory();
  // Completion time carries IRQ-entry jitter; sampling cadence remains fixed.
  fillFrames({0U, 0U, 3300U, 3300U, false}, kDmaFrames, 0U);
  VP37CurrentPulseResult result;
  const hal_status_t status =
      completeBlock(completed + framesToUs(kDmaFrames) + 20U, &result);
  TEST_ASSERT_TRUE((status == HAL_OK) || (status == HAL_EAGAIN));
  TEST_ASSERT_TRUE(result.supplyLatestValid);

  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStop());
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStart());
  assertFreshBlockAfterDiscontinuity(completed + (2U * framesToUs(kDmaFrames)));
}

void test_scan_history_keeps_period_timestamps_stable_despite_irq_jitter(void) {
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStart());
  const uint32_t start = 100000U;
  uint32_t previousCycle = 0U;
  uint32_t repeated = 0U;
  VP37CurrentPulseResult previous = {};
  for (uint32_t i = 0U; i < 12U; i++) {
    const uint16_t supply = (uint16_t)(3000U + i * 10U);
    fillFrames({3U, 0U, supply, supply, true}, kDmaFrames, i * kDmaFrames);
    const uint32_t total = (i + 1U) * kDmaFrames;
    const uint32_t idealCompletion = start + framesToUs(total);
    const uint32_t actualCompletion =
        (i % 2U) == 0U ? idealCompletion + 20U : idealCompletion - 20U;
    VP37CurrentPulseResult result;
    const hal_status_t status = completeBlock(actualCompletion, &result);
    TEST_ASSERT_TRUE((status == HAL_OK) || (status == HAL_EAGAIN));
    if (result.waveformValid) {
      // The first IRQ anchors time once; later jitter cannot retime a cycle.
      TEST_ASSERT_EQUAL_UINT32(start + 20U +
                                   framesToUs(newestPeriodStart(3U, total)),
                               result.cycleStartUs);
      if (result.cycleStartUs == previousCycle) {
        repeated++;
        TEST_ASSERT_EQUAL_FLOAT(previous.meanAmps, result.meanAmps);
        TEST_ASSERT_EQUAL_FLOAT(previous.supplyVolts, result.supplyVolts);
        TEST_ASSERT_EQUAL_MEMORY(previous.profileAmps, result.profileAmps,
                                 sizeof(result.profileAmps));
        TEST_ASSERT_GREATER_THAN_UINT32(previous.supplyLatestUs,
                                        result.supplyLatestUs);
        TEST_ASSERT_GREATER_THAN_FLOAT(previous.supplyLatestVolts,
                                       result.supplyLatestVolts);
      }
      previousCycle = result.cycleStartUs;
      previous = result;
    }
  }
  if (kDmaFrames < kPeriodFrames) {
    TEST_ASSERT_GREATER_THAN_UINT32(0U, repeated);
  }
  // A cached pulse must expire when its frames leave the retained history.
  const uint32_t blocks = (kBlockFrames + kDmaFrames - 1U) / kDmaFrames + 2U;
  VP37CurrentPulseResult expired;
  hal_status_t status = HAL_OK;
  for (uint32_t i = 12U; i < 12U + blocks; ++i) {
    fillFrames({0U, 0U, 3300U, 3300U, false}, kDmaFrames, i * kDmaFrames);
    status = completeBlock(start + framesToUs((i + 1U) * kDmaFrames), &expired);
  }
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, status);
  TEST_ASSERT_FALSE(expired.waveformValid);
  TEST_ASSERT_TRUE(expired.supplyLatestValid);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pulse_analyze_guards_edges_and_winsorizes_a_switching_spike);
  RUN_TEST(test_pulse_analyze_rejects_bad_timing_and_clipping);
  RUN_TEST(test_pulse_analyze_handles_wrap_and_rejects_samples_outside_cycle);
  RUN_TEST(
      test_pulse_profile_preserves_time_order_and_rejects_invalid_waveforms);
  RUN_TEST(test_scan_reduce_rejects_bad_view_zero_and_blocks_without_edges);
  RUN_TEST(test_scan_reduce_measures_the_newest_full_period);
  RUN_TEST(
      test_scan_reduce_preserves_integer_fractional_and_wrapped_timestamps);
  RUN_TEST(test_scan_reduce_keeps_supply_and_current_validity_separate);
  RUN_TEST(test_scan_latch_tracks_falling_edges_when_duty_changes);
  RUN_TEST(test_scan_reduce_ignores_single_frame_spikes_and_dropouts);
  RUN_TEST(test_scan_collect_takes_each_mock_block_once);
  RUN_TEST(test_streaming_compensation_matches_raw_reduction_at_transfer_gaps);
  RUN_TEST(test_scan_poll_retains_fast_blocks_between_reductions);
  RUN_TEST(
      test_latest_supply_tracks_the_end_of_history_and_its_center_timestamp);
  RUN_TEST(test_latest_supply_uses_measured_period_and_rejects_phase_ripple);
  RUN_TEST(test_latest_supply_does_not_require_current_edges_or_shunt_zero);
  RUN_TEST(
      test_scan_history_publishes_fresh_supply_each_block_across_time_wrap);
  RUN_TEST(test_scan_history_discards_missing_blocks_and_timestamp_gaps);
  RUN_TEST(
      test_scan_restart_discards_history_but_small_irq_jitter_preserves_it);
  RUN_TEST(test_scan_history_keeps_period_timestamps_stable_despite_irq_jitter);
  return UNITY_END();
}
