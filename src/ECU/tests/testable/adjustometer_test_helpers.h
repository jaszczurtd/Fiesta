#pragma once
#include "../common/adjustometer_feedback.h"
#include "hal/impl/.mock/hal_mock.h"

static inline void injectFastAdjustometer(const adjustometer_feedback_t &sample,
                                          uint8_t sequence = 2U) {
  uint8_t frame[ADJUSTOMETER_TOTAL_REG_COUNT - ADJUSTOMETER_EXT_REG_START] = {
      0};
  adjustometer_feedback_encode(frame + ADJUSTOMETER_FEEDBACK_START -
                                   ADJUSTOMETER_EXT_REG_START,
                               &sample, sequence);
  hal_mock_i2c_inject_rx(frame, (int)COUNTOF(frame));
}
