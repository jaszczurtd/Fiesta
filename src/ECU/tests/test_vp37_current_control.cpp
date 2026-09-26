#include "hal/impl/.mock/hal_mock.h"
#include "unity.h"
#include "vp37_internal.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static constexpr uint32_t kPeriodUs = 1000000U / VP37_PWM_FREQUENCY_HZ;
static constexpr float kControlDt = 0.005f;
static constexpr float kNominalPwm = 820.0f;
static constexpr int32_t kDeliveredPwm = 700;
static VP37Pump s_pump;

void setUp(void) {
  std::memset(&s_pump, 0, sizeof(s_pump));
  hal_mock_set_micros(0U);
  s_pump.currentControl.enabled = true;
  s_pump.thermal.observationEnabled = true;
  s_pump.scan.running = true;
  s_pump.supply.localScale = 1.034f;
  s_pump.output.finalPWM = kDeliveredPwm;
  s_pump.demand.requestedPercent = 60.0f;
  s_pump.demand.desiredPosition = 5000.0f;
  VP37_resetCurrentControl(&s_pump);
}

void tearDown(void) { (void)VP37_currentScanStop(); }

static float referenceAmps(float nominal, float scale) {
  return nominal * 12.0f / (2047.0f * 1.2f * scale);
}

static void capture(uint32_t latchedUs, float amps,
                    int32_t duty = kDeliveredPwm) {
  VP37CurrentPulseResult &sample = s_pump.scan.cycleResult;
  sample = {};
  sample.periodUs = kPeriodUs;
  sample.onTimeUs =
      (uint32_t)(((uint64_t)kPeriodUs * (uint32_t)duty) / PWM_RESOLUTION);
  sample.pwmCommand = duty;
  sample.latchUs = latchedUs;
  sample.latchPeriodUs = kPeriodUs;
  sample.latchedPwm = duty;
  sample.latchValid = true;
  sample.cycleStartUs = latchedUs + kPeriodUs - sample.onTimeUs;
  sample.meanAmps = amps;
  sample.zeroValid = true;
  sample.waveformValid = true;
  s_pump.scan.cycleResultStatus = HAL_OK;
}

static void updateAt(uint32_t nowUs) {
  hal_mock_set_micros(nowUs);
  VP37_updateCurrentControl(&s_pump, kControlDt);
}

static void record(uint32_t writtenUs, float nominal = kNominalPwm,
                   int32_t delivered = kDeliveredPwm) {
  VP37_recordCurrentCommand(&s_pump, nominal, delivered, writtenUs);
}

void test_current_control_uses_the_command_latched_before_the_on_phase(void) {
  const float expectedAmps = referenceAmps(kNominalPwm, 1.034f);
  record(98500U);
  // This write is before the ON rise, but after its preceding hardware wrap.
  // It therefore cannot have produced the observed current pulse.
  s_pump.supply.localScale = 0.95f;
  record(104000U, 1100.0f, 920);
  capture(100000U, expectedAmps - 0.25f);
  // A duty change can alter rise-to-rise duty without changing this pulse's
  // fall-to-fall duty; the latter owns the hardware command.
  s_pump.scan.cycleResult.pwmCommand += 30;
  updateAt(112000U);

  TEST_ASSERT_TRUE(s_pump.currentControl.active);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, expectedAmps,
                           s_pump.currentControl.sampleTargetAmps);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, s_pump.currentControl.errorAmps);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, s_pump.currentControl.correctionPwm);
  TEST_ASSERT_EQUAL_UINT32(112000U - (s_pump.scan.cycleResult.cycleStartUs +
                                      s_pump.scan.cycleResult.onTimeUs / 2U),
                           s_pump.currentControl.sampleAgeUs);
}

void test_current_control_preserves_the_baseline_voltage_scale(void) {
  record(98500U);
  capture(100000U, referenceAmps(kNominalPwm, 1.034f));
  updateAt(112000U);
  TEST_ASSERT_TRUE(s_pump.currentControl.active);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s_pump.currentControl.requestedPwm);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s_pump.currentControl.correctionPwm);
}

void test_current_control_rejects_writes_ambiguous_at_pwm_wrap(void) {
  const int32_t offsets[] = {-100, -1, 0, 1, 100};
  for (size_t i = 0U; i < COUNTOF(offsets); ++i) {
    VP37_resetCurrentControl(&s_pump);
    record(97000U);
    record((uint32_t)(100000 + offsets[i]), 900.0f, 710);
    capture(100000U, 3.0f);
    updateAt(112000U);
    TEST_ASSERT_FALSE(s_pump.currentControl.active);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s_pump.currentControl.requestedPwm);
  }
}

void test_current_control_rejects_missing_or_mismatched_command_history(void) {
  capture(100000U, 3.0f);
  updateAt(112000U);
  TEST_ASSERT_FALSE(s_pump.currentControl.active);

  record(98500U);
  capture(107692U, 3.0f, kDeliveredPwm + 25);
  updateAt(119692U);
  TEST_ASSERT_FALSE(s_pump.currentControl.active);

  // A future record cannot serve as a substitute for the missing old command.
  VP37_resetCurrentControl(&s_pump);
  record(105000U);
  capture(100000U, 3.0f);
  updateAt(112000U);
  TEST_ASSERT_FALSE(s_pump.currentControl.active);
}

void test_current_control_repeated_capture_does_not_recompute_the_error(void) {
  record(98500U);
  capture(100000U, referenceAmps(kNominalPwm, 1.034f) - 0.3f);
  updateAt(112000U);
  const float requested = s_pump.currentControl.requestedPwm;
  TEST_ASSERT_GREATER_THAN_FLOAT(10.0f, requested);
  // Duplicate publication must neither retrain nor amplify a previous error.
  s_pump.scan.cycleResult.meanAmps += 1.0f;
  updateAt(117000U);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, requested,
                           s_pump.currentControl.requestedPwm);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, s_pump.currentControl.correctionPwm);

  const uint32_t midpoint = s_pump.scan.cycleResult.cycleStartUs +
                            s_pump.scan.cycleResult.onTimeUs / 2U;
  updateAt(midpoint + 25000U);
  TEST_ASSERT_FALSE(s_pump.currentControl.active);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s_pump.currentControl.requestedPwm);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, s_pump.currentControl.correctionPwm);
}

void test_current_control_bounds_both_polarities_and_fades_invalid_samples(
    void) {
  const float measured[] = {1.6f, 8.0f};
  const float expected[] = {40.0f, -40.0f};
  for (size_t i = 0U; i < COUNTOF(measured); ++i) {
    VP37_resetCurrentControl(&s_pump);
    record(98500U);
    for (uint32_t n = 0U; n < 9U; ++n) {
      const uint32_t rise = 100000U + n * kPeriodUs;
      record(rise - 1500U);
      capture(rise, measured[i]);
      updateAt(rise + 12000U);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected[i],
                             s_pump.currentControl.requestedPwm);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected[i],
                             s_pump.currentControl.correctionPwm);
    s_pump.scan.cycleResult.waveformValid = false;
    VP37_updateCurrentControl(&s_pump, kControlDt);
    TEST_ASSERT_FALSE(s_pump.currentControl.active);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected[i] * 0.875f,
                             s_pump.currentControl.correctionPwm);
  }
}

void test_current_control_faults_and_disabled_observation_never_add_drive(
    void) {
  for (uint32_t fault = 0U; fault < 8U; ++fault) {
    setUp();
    record(98500U);
    capture(100000U, 2.0f);
    switch (fault) {
    case 0U:
      s_pump.scan.running = false;
      break;
    case 1U:
      s_pump.scan.cycleResultStatus = HAL_EAGAIN;
      break;
    case 2U:
      s_pump.scan.cycleResult.waveformValid = false;
      break;
    case 3U:
      s_pump.thermal.observationEnabled = false;
      break;
    case 4U:
      s_pump.supply.frozen = true;
      break;
    case 5U:
      s_pump.scan.cycleResult.meanAmps = 1.49f;
      break;
    case 6U:
      s_pump.scan.cycleResult.latchValid = false;
      break;
    default:
      s_pump.scan.cycleResult.meanAmps = NAN;
      break;
    }
    updateAt(112000U);
    TEST_ASSERT_FALSE(s_pump.currentControl.active);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s_pump.currentControl.correctionPwm);
  }
}

void test_current_control_rest_clears_trim_and_history_immediately(void) {
  record(98500U);
  capture(100000U, 2.0f);
  updateAt(112000U);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, s_pump.currentControl.correctionPwm);
  s_pump.demand.atRest = true;
  updateAt(117000U);
  TEST_ASSERT_FALSE(s_pump.currentControl.active);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s_pump.currentControl.correctionPwm);
  s_pump.demand.atRest = false;
  capture(107692U, 2.0f);
  updateAt(119692U);
  TEST_ASSERT_FALSE(s_pump.currentControl.active);
}

void test_current_control_handles_clock_wrap_and_expired_ring_history(void) {
  const uint32_t rise = UINT32_MAX - 4000U;
  record(rise - 1500U);
  capture(rise, referenceAmps(kNominalPwm, 1.034f));
  updateAt(rise + 12000U);
  TEST_ASSERT_TRUE(s_pump.currentControl.active);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, s_pump.currentControl.correctionPwm);

  VP37_resetCurrentControl(&s_pump);
  record(98500U);
  for (uint32_t n = 0U; n < 20U; ++n) {
    record(101000U + n * 500U, 900.0f, 760);
  }
  capture(100000U, 3.0f);
  updateAt(112000U);
  TEST_ASSERT_FALSE(s_pump.currentControl.active);
}

enum class Disturbance { Resistance, Supply, Demand };

struct SimulationResult {
  float rmsAmps;
  float peakCorrection;
  uint32_t observations;
  uint32_t activeSteps;
  uint32_t missingLatchSteps;
};

// Encode an ideal ADC voltage through the inverse of the production transfer
// correction. The model's coil current is not already an ADC-corrected code.
static uint16_t adcCode(float volts) {
  const int ideal = (int)std::lround(volts * 4096.0f / 3.3f);
  int low = 0;
  int high = 4095;
  while (low < high) {
    const int middle = (low + high) / 2;
    if (hal_adc_compensate_rp2040_12bit(middle) < ideal) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return (uint16_t)low;
}

static float railAt(uint32_t us, Disturbance disturbance) {
  if ((disturbance != Disturbance::Supply) || us < 800000U) {
    return 12.0f;
  }
  const uint32_t phase = (us - 800000U) % 600000U;
  if (phase < 200000U) {
    return 12.0f + (float)phase * 0.00002f;
  }
  if (phase < 400000U) {
    return 16.0f - (float)(phase - 200000U) * 0.00003f;
  }
  return 10.0f + (float)(phase - 400000U) * 0.00001f;
}

static float demandedAmps(uint32_t us, Disturbance disturbance) {
  if ((disturbance == Disturbance::Demand) && us >= 800000U && us < 1400000U) {
    // Fast movement exercises old captures against newly written targets.
    return 3.0f;
  }
  return 4.0f;
}

static SimulationResult simulateCurrentLoop(bool enabled,
                                            Disturbance disturbance,
                                            uint32_t pwmPhaseUs,
                                            float inductance) {
  setUp();
  s_pump.currentControl.enabled = enabled;
  s_pump.supply.localScale = 1.0f;
  s_pump.supply.localReady = true;
  s_pump.supply.ready = true;
  s_pump.supply.heldVolts = 12.0f;
  s_pump.supply.localVolts = 12.0f;
  s_pump.supply.lastVolts = 12.0f;
  s_pump.supply.cycleEnabled = true;
  hal_mock_adc_inject(ADC_VP37_CURRENT_PIN, 0);
  VP37_currentSenseInit();
  hal_mock_set_micros(0U);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStart());

  uint16_t frames[VP37_CURRENT_SCAN_BLOCK_FRAMES * VP37_CURRENT_SCAN_PINS] = {};
  const uint32_t frameUs = VP37_CURRENT_SCAN_FRAME_NS / 1000U;
  const uint32_t blockUs = VP37_CURRENT_SCAN_BLOCK_FRAMES * frameUs;
  uint32_t frame = 0U;
  uint32_t nextWrap = pwmPhaseUs;
  uint32_t cycleStart = pwmPhaseUs;
  uint32_t lastMeasuredCycle = UINT32_MAX;
  int32_t pendingPwm = 819;
  int32_t latchedPwm = pendingPwm;
  float coilAmps = 4.0f;
  double squaredError = 0.0;
  SimulationResult result = {};
  // A switched RL coil with a zero-drop recirculation path. Its current
  // continues during OFF, but the source shunt sees zero. This test isolates
  // the electrical loop; it does not model actuator mechanics or position PID.
  for (uint32_t us = 0U; us <= 2000000U; us += 2U) {
    if (us == nextWrap) {
      latchedPwm = pendingPwm;
      cycleStart = us;
      nextWrap += kPeriodUs;
    }
    if ((us > 0U) && ((us % blockUs) == 0U)) {
      hal_mock_set_micros(us);
      TEST_ASSERT_EQUAL_INT(
          HAL_OK,
          hal_mock_adc_scan_complete(frames, VP37_CURRENT_SCAN_BLOCK_FRAMES));
      TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_acquireCurrentScan(&s_pump));
      frame = 0U;
    }
    if ((us % 5000U) == 0U) {
      hal_mock_set_micros(us);
      (void)VP37_serviceCurrentScan(&s_pump);
      VP37_updateVoltageCorrection(&s_pump, kControlDt);
      VP37_updateCurrentControl(&s_pump, kControlDt);
      const float nominal =
          demandedAmps(us, disturbance) * 2047.0f * 1.2f / 12.0f;
      pendingPwm =
          (int32_t)std::lround((nominal + s_pump.currentControl.correctionPwm) *
                               s_pump.supply.correction);
      pendingPwm = hal_constrain(pendingPwm, 0, PWM_RESOLUTION);
      s_pump.output.finalPWM = pendingPwm;
      VP37_recordCurrentCommand(&s_pump, nominal, pendingPwm, us);
      result.peakCorrection =
          std::fmax(result.peakCorrection,
                    std::fabs(s_pump.currentControl.correctionPwm));
      result.activeSteps += s_pump.currentControl.active ? 1U : 0U;
      const VP37CurrentPulseResult &sample = s_pump.scan.cycleResult;
      if (us >= 100000U && sample.waveformValid && !sample.latchValid) {
        result.missingLatchSteps++;
      }
      if (sample.waveformValid && sample.cycleStartUs != lastMeasuredCycle) {
        lastMeasuredCycle = sample.cycleStartUs;
        if (sample.cycleStartUs >= 1000000U) {
          const float error =
              sample.meanAmps - demandedAmps(sample.cycleStartUs, disturbance);
          squaredError += (double)error * error;
          result.observations++;
        }
      }
    }
    // valToPWM writes PWM_RESOLUTION - drive. The external driver is active
    // LOW, so physical ON starts after compare and ends at the next wrap.
    const bool on =
        (us >= pwmPhaseUs) &&
        ((uint64_t)(us - cycleStart) * PWM_RESOLUTION >=
         (uint64_t)(PWM_RESOLUTION - (uint32_t)latchedPwm) * kPeriodUs);
    if ((us % frameUs) == 0U) {
      frames[frame * VP37_CURRENT_SCAN_PINS] =
          adcCode(on ? coilAmps * 0.22f : 0.0f);
      frames[frame * VP37_CURRENT_SCAN_PINS + 1U] = 1234U;
      const float divider =
          (float)V_DIVIDER_R2 / (float)(V_DIVIDER_R1 + V_DIVIDER_R2);
      frames[frame * VP37_CURRENT_SCAN_PINS + 2U] =
          adcCode(railAt(us + 16U, disturbance) * divider);
      frame++;
    }
    const float resistance =
        ((disturbance == Disturbance::Resistance) && us >= 800000U) ? 1.32f
                                                                    : 1.2f;
    coilAmps +=
        ((on ? railAt(us, disturbance) : 0.0f) - coilAmps * resistance) *
        0.000002f / inductance;
  }
  TEST_ASSERT_GREATER_THAN_UINT32(80U, result.observations);
  result.rmsAmps = (float)std::sqrt(squaredError / result.observations);
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_currentScanStop());
  return result;
}

void test_current_control_attenuates_a_causal_switched_coil_disturbance(void) {
  const float inductances[] = {0.01f, 0.03f};
  for (size_t i = 0U; i < COUNTOF(inductances); ++i) {
    for (uint32_t phase = 0U; phase < 7600U; phase += 2000U) {
      const SimulationResult open = simulateCurrentLoop(
          false, Disturbance::Resistance, phase, inductances[i]);
      const SimulationResult closed = simulateCurrentLoop(
          true, Disturbance::Resistance, phase, inductances[i]);
      TEST_ASSERT_GREATER_THAN_UINT32(200U, closed.activeSteps);
      TEST_ASSERT_EQUAL_UINT32(0U, closed.missingLatchSteps);
      TEST_ASSERT_LESS_THAN_FLOAT(open.rmsAmps * 0.94f, closed.rmsAmps);
      TEST_ASSERT_LESS_OR_EQUAL_FLOAT(40.001f, closed.peakCorrection);
      std::printf("current model resistance L=%.3fH phase=%lu open=%.5fA "
                  "closed=%.5fA active=%lu/401 missing_latch=%lu\n",
                  (double)inductances[i], (unsigned long)phase,
                  (double)open.rmsAmps, (double)closed.rmsAmps,
                  (unsigned long)closed.activeSteps,
                  (unsigned long)closed.missingLatchSteps);
    }
  }
}

void test_current_control_stays_bounded_during_supply_and_demand_changes(void) {
  const Disturbance disturbances[] = {Disturbance::Supply, Disturbance::Demand};
  for (size_t i = 0U; i < COUNTOF(disturbances); ++i) {
    for (uint32_t phase = 0U; phase < 7600U; phase += 2000U) {
      const SimulationResult open =
          simulateCurrentLoop(false, disturbances[i], phase, 0.03f);
      const SimulationResult closed =
          simulateCurrentLoop(true, disturbances[i], phase, 0.03f);
      // Voltage prediction already handles most supply disturbance. Historical
      // current feedback must not add a second large transient on target steps.
      TEST_ASSERT_LESS_THAN_FLOAT(open.rmsAmps + 0.04f, closed.rmsAmps);
      TEST_ASSERT_LESS_OR_EQUAL_FLOAT(40.001f, closed.peakCorrection);
      std::printf(
          "current model profile=%u phase=%lu open=%.5fA closed=%.5fA\n",
          (unsigned)i, (unsigned long)phase, (double)open.rmsAmps,
          (double)closed.rmsAmps);
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_current_control_uses_the_command_latched_before_the_on_phase);
  RUN_TEST(test_current_control_preserves_the_baseline_voltage_scale);
  RUN_TEST(test_current_control_rejects_writes_ambiguous_at_pwm_wrap);
  RUN_TEST(test_current_control_rejects_missing_or_mismatched_command_history);
  RUN_TEST(test_current_control_repeated_capture_does_not_recompute_the_error);
  RUN_TEST(
      test_current_control_bounds_both_polarities_and_fades_invalid_samples);
  RUN_TEST(
      test_current_control_faults_and_disabled_observation_never_add_drive);
  RUN_TEST(test_current_control_rest_clears_trim_and_history_immediately);
  RUN_TEST(test_current_control_handles_clock_wrap_and_expired_ring_history);
  RUN_TEST(test_current_control_attenuates_a_causal_switched_coil_disturbance);
  RUN_TEST(test_current_control_stays_bounded_during_supply_and_demand_changes);
  return UNITY_END();
}
