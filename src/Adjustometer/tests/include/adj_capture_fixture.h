#pragma once
#include "hal/impl/.mock/hal_mock.h"
#include "sensors.h"

/* Independent 16 MHz oscillator, including sub-microsecond periods. */
static inline void adj_test_capture_pulses(uint32_t count, uint32_t frequency) {
  const uint32_t period_ticks = (16000000U + frequency / 2U) / frequency;
  uint32_t ticks = hal_micros() * 16U;
  uint32_t fraction = 0U;
  for (uint32_t i = 0U; i < count; ++i) {
    ticks += period_ticks;
    fraction += period_ticks;
    hal_mock_advance_micros(fraction / 16U);
    fraction %= 16U;
    (void)hal_mock_pulse_capture_edge(ticks, hal_micros());
    updateAdjustometerCapture();
  }
}
