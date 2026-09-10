#pragma once
#include "../common/adjustometer_feedback.h"
#include "hal/impl/.mock/hal_mock.h"

static inline void injectFastAdjustometer(const adjustometer_feedback_t &sample,
                                          uint8_t sequence = 2U) {
  uint8_t frame[ADJUSTOMETER_FEEDBACK_BYTES];
  adjustometer_feedback_encode(frame, &sample, sequence);
  hal_mock_i2c_inject_rx(frame, (int)COUNTOF(frame));
}
