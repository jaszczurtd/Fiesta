#include "adjustometer_unit_testing.h"

#include "../sensors.c"

extern "C" void adj_test_sensors_reset_state(void) {
  resetSensorsState();
}

extern "C" void adj_test_sensors_get_state(adj_sensors_test_state_t *state) {
  if (state == 0) {
    return;
  }

  state->pulse = __atomic_load_n(&adjustometerPulse, __ATOMIC_ACQUIRE);
  state->lastEdgeUs = __atomic_load_n(&adjustometerLastEdgeUs, __ATOMIC_ACQUIRE);
  state->signalHz = __atomic_load_n(&adjustometerSignalHz, __ATOMIC_ACQUIRE);
  state->windowStartUs = adjustometerWindowStartUs;
  state->windowCount = adjustometerWindowCount;
  state->filteredHz = adjustometerFilteredHz;
  state->baselineStartUs = adjustometerBaselineStartUs;
  state->baselineEstimate = adjustometerBaselineEstimate;
  state->baselineStableWindows = adjustometerBaselineStableWindows;
  state->baseline = adjustometerBaseline;
  state->baselineReady = __atomic_load_n(&adjustometerBaselineReady, __ATOMIC_ACQUIRE);
  state->verifying = adjustometerVerifying;
  state->verifyStartUs = adjustometerVerifyStartUs;
  state->zeroHold = adjustometerZeroHold;
  state->zeroCandidateSign = adjustometerZeroCandidateSign;
  state->zeroCandidateWindows = adjustometerZeroCandidateWindows;
  state->filteredFuelTemp = filteredFuelTemp;
  state->filteredVoltage = filteredVoltage;
}

extern "C" void adj_test_sensors_set_state(const adj_sensors_test_state_t *state) {
  if (state == 0) {
    return;
  }

  __atomic_store_n(&adjustometerPulse, state->pulse, __ATOMIC_RELEASE);
  __atomic_store_n(&adjustometerLastEdgeUs, state->lastEdgeUs, __ATOMIC_RELEASE);
  __atomic_store_n(&adjustometerSignalHz, state->signalHz, __ATOMIC_RELEASE);
  adjustometerWindowStartUs = state->windowStartUs;
  adjustometerWindowCount = state->windowCount;
  adjustometerFilteredHz = state->filteredHz;
  adjustometerBaselineStartUs = state->baselineStartUs;
  adjustometerBaselineEstimate = state->baselineEstimate;
  adjustometerBaselineStableWindows = state->baselineStableWindows;
  adjustometerBaseline = state->baseline;
  __atomic_store_n(&adjustometerBaselineReady, state->baselineReady, __ATOMIC_RELEASE);
  adjustometerVerifying = state->verifying;
  adjustometerVerifyStartUs = state->verifyStartUs;
  adjustometerZeroHold = state->zeroHold;
  adjustometerZeroCandidateSign = state->zeroCandidateSign;
  adjustometerZeroCandidateWindows = state->zeroCandidateWindows;
  filteredFuelTemp = state->filteredFuelTemp;
  filteredVoltage = state->filteredVoltage;
}

extern "C" void adj_test_sensors_count_edge(void) {
  countAdjustometerPulses();
}

extern "C" uint32_t adj_test_sensors_apply_adjustometer_ema(uint32_t rawHz, uint32_t filteredHz) {
  return applyAdjustometerEma(rawHz, filteredHz);
}

extern "C" float adj_test_sensors_apply_adc_ema(float raw, float prev) {
  return adcEma(raw, prev);
}

extern "C" bool adj_test_sensors_is_signal_lost(void) {
  return isSignalLost();
}