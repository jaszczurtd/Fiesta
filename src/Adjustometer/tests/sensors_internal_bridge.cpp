#include "adjustometer_unit_testing.h"

#include "../sensors.c"
#include "hal/impl/.mock/hal_mock.h"

extern "C" void adj_test_sensors_reset_state(void) { initSensors(); }

extern "C" void adj_test_sensors_get_state(adj_sensors_test_state_t *state) {
  if (state == 0) {
    return;
  }

  state->pulse = HAL_ATOMIC_LOAD(&adjustometerPulse, HAL_ATOMIC_ACQUIRE);
  state->lastEdgeUs =
      HAL_ATOMIC_LOAD(&adjustometerLastEdgeUs, HAL_ATOMIC_ACQUIRE);
  state->signalHz = HAL_ATOMIC_LOAD(&adjustometerSignalHz, HAL_ATOMIC_ACQUIRE);
  state->filteredHz = adjustometerFilteredHz;
  state->baselineStartUs = adjustometerBaselineStartUs;
  state->baselineEstimate = adjustometerBaselineEstimate;
  state->baselineStableWindows = adjustometerBaselineStableWindows;
  state->baseline = adjustometerBaseline;
  state->baselineReady =
      HAL_ATOMIC_LOAD(&adjustometerBaselineReady, HAL_ATOMIC_ACQUIRE);
  state->verifying = adjustometerVerifying;
  state->verifyStartUs = adjustometerVerifyStartUs;
  state->zeroHold = adjustometerZeroHold;
  state->zeroCandidateSign = adjustometerZeroCandidateSign;
  state->zeroCandidateWindows = adjustometerZeroCandidateWindows;
  state->filteredFuelTemp = filteredFuelTemp;
  state->filteredVoltage = filteredVoltage;
}

extern "C" void
adj_test_sensors_set_state(const adj_sensors_test_state_t *state) {
  if (state == 0) {
    return;
  }

  HAL_ATOMIC_STORE(&adjustometerPulse, state->pulse, HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&adjustometerLastEdgeUs, state->lastEdgeUs,
                   HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&adjustometerSignalHz, state->signalHz, HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&captureHealthy, state->lastEdgeUs != 0U,
                   HAL_ATOMIC_RELEASE);
  adjustometerFilteredHz = state->filteredHz;
  adjustometerBaselineStartUs = state->baselineStartUs;
  adjustometerBaselineEstimate = state->baselineEstimate;
  adjustometerBaselineStableWindows = state->baselineStableWindows;
  adjustometerBaseline = state->baseline;
  HAL_ATOMIC_STORE(&adjustometerBaselineReady, state->baselineReady,
                   HAL_ATOMIC_RELEASE);
  adjustometerVerifying = state->verifying;
  adjustometerVerifyStartUs = state->verifyStartUs;
  adjustometerZeroHold = state->zeroHold;
  adjustometerZeroCandidateSign = state->zeroCandidateSign;
  adjustometerZeroCandidateWindows = state->zeroCandidateWindows;
  filteredFuelTemp = state->filteredFuelTemp;
  filteredVoltage = state->filteredVoltage;
}

extern "C" void adj_test_sensors_count_edge(void) {
  hal_mock_pulse_capture_edge(hal_micros() * 16U, hal_micros());
  updateAdjustometerCapture();
}

extern "C" uint32_t
adj_test_sensors_apply_adjustometer_ema(uint32_t rawHz, uint32_t filteredHz) {
  return applyAdjustometerEma(rawHz, filteredHz);
}

extern "C" float adj_test_sensors_apply_adc_ema(float raw, float prev) {
  return adcEma(raw, prev);
}

extern "C" bool adj_test_sensors_is_signal_lost(void) { return isSignalLost(); }

extern "C" void adj_test_sensors_process_frequency(uint32_t rawHz,
                                                   uint32_t nowUs) {
  HAL_ATOMIC_STORE(&adjustometerLastEdgeUs, nowUs, HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&captureHealthy, true, HAL_ATOMIC_RELEASE);
  processAdjustometerFrequency(rawHz, nowUs);
}
