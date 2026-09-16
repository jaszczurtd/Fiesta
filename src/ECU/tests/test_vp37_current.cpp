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

void tearDown(void) {}

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

// Frame layout of the ECU scan on RP: shunt, sensor mux, supply.
static constexpr uint8_t kScanPins = VP37_CURRENT_SCAN_PINS;
static constexpr uint8_t kShuntPosition = 0U;
static constexpr uint8_t kSupplyPosition = 2U;
static constexpr uint32_t kFrameNs = VP37_CURRENT_SCAN_FRAME_NS;
static constexpr uint32_t kBlockFrames = VP37_CURRENT_SCAN_BLOCK_FRAMES;
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

static void fillBlock(const Waveform &w) {
  for (uint32_t k = 0U; k < kBlockFrames; k++) {
    const uint32_t phase =
        (k + kPeriodFrames - (w.offsetFrames % kPeriodFrames)) % kPeriodFrames;
    const bool on = w.edges && (phase < kOnFrames);
    uint16_t shunt = w.zeroRaw;
    if (on) {
      const float amps = 3.0f + (2.0f * (float)phase / (float)(kOnFrames - 1U));
      shunt = (uint16_t)(w.zeroRaw + ampsToRaw(amps));
    }
    s_block[(k * kScanPins) + kShuntPosition] = shunt;
    s_block[(k * kScanPins) + 1U] = 1234U;
    s_block[(k * kScanPins) + kSupplyPosition] = on ? w.supplyOn : w.supplyOff;
  }
}

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

static uint32_t framesToUs(uint32_t frames) {
  return (uint32_t)((((uint64_t)frames * kFrameNs) + 500U) / 1000U);
}

/* Newest rising edge whose following rising edge is confirmed inside the
   block, i.e. its confirmation window still fits; a block starting inside an
   ON phase has no detectable edge at frame 0. */
static uint32_t newestPeriodStart(uint32_t offset) {
  uint32_t best = UINT32_MAX;
  for (uint32_t rise = offset;
       rise + kPeriodFrames + VP37_CURRENT_GATE_CONFIRM_FRAMES - 1U <=
       kBlockFrames - 1U;
       rise += kPeriodFrames) {
    if (rise >= 1U) {
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

  // Without gate edges there is no period, so neither the waveform nor the
  // supply mean may be presented as usable, and no field may read as amperes.
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 16);
  VP37_currentSenseInit();
  fillBlock({0U, 16U, 3000U, 3100U, false});
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_TRUE(result.zeroValid);
  TEST_ASSERT_EQUAL_UINT16(16U, result.zeroRaw);
  TEST_ASSERT_FALSE(result.waveformValid);
  TEST_ASSERT_FALSE(result.supplyValid);
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
  TEST_ASSERT_TRUE(newest + (2U * kPeriodFrames) +
                       VP37_CURRENT_GATE_CONFIRM_FRAMES - 1U >
                   kBlockFrames - 1U);
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
  s_block[((newest + kOnFrames + 1U) * kScanPins) + kSupplyPosition] =
      VP37_CURRENT_ADC_MAX_RAW;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanReduce(&view, &result));
  TEST_ASSERT_TRUE(result.waveformValid);
  TEST_ASSERT_FALSE(result.supplyValid);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, result.supplyVolts);
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

  fillBlock({3U, 16U, 3000U, 3100U, true});
  hal_mock_set_micros(500000U);
  TEST_ASSERT_EQUAL_INT(HAL_OK,
                        hal_mock_adc_scan_complete(s_block, kBlockFrames));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanCollect(&result, &sequence));
  TEST_ASSERT_EQUAL_UINT32(1U, sequence);
  TEST_ASSERT_TRUE(result.waveformValid);
  TEST_ASSERT_EQUAL_UINT32(500000U - framesToUs(kBlockFrames) +
                               framesToUs(newestPeriodStart(3U)),
                           result.cycleStartUs);
  // A block is handed out once.
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        VP37_currentScanCollect(&result, &sequence));
  TEST_ASSERT_EQUAL_UINT32(0U, sequence);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStop());
  TEST_ASSERT_EQUAL_UINT32(0U, VP37_currentScanFrameNs());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pulse_analyze_guards_edges_and_winsorizes_a_switching_spike);
  RUN_TEST(test_pulse_analyze_rejects_bad_timing_and_clipping);
  RUN_TEST(test_pulse_analyze_handles_wrap_and_rejects_samples_outside_cycle);
  RUN_TEST(test_scan_reduce_rejects_bad_view_zero_and_blocks_without_edges);
  RUN_TEST(test_scan_reduce_measures_the_newest_full_period);
  RUN_TEST(test_scan_reduce_keeps_supply_and_current_validity_separate);
  RUN_TEST(test_scan_reduce_ignores_single_frame_spikes_and_dropouts);
  RUN_TEST(test_scan_collect_takes_each_mock_block_once);
  return UNITY_END();
}
