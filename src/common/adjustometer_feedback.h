#pragma once

#include "adjustometer_protocol.h"
#include <hal/core/hal_status.h>
#include <hal/core/jh_endian.h>
#include <stddef.h>

/** @brief One oscillator window and cached auxiliary telemetry. Frequencies in
 * Hz. */
typedef struct {
  uint32_t rawHz, filteredHz, baselineHz;
  uint32_t number,
      measuredUs;  /**< Window number and Adjustometer clock timestamp. */
  uint16_t ageUs;  /**< Age at publication, saturated at UINT16_MAX. */
  int16_t pulseHz; /**< Filtered deviation magnitude after zero hold. */
  uint8_t voltage, fuelTemp, status;
} adjustometer_feedback_t;

/** @brief Encode a non-NULL sample into a non-NULL 30-byte frame; sequence must
 * be even. */
static inline void adjustometer_feedback_encode(
    uint8_t *frame, const adjustometer_feedback_t *sample, uint8_t sequence) {
  frame[0] = ADJUSTOMETER_FEEDBACK_VERSION;
  frame[1] = sequence;
  jh_store_be16(frame + 2, (uint16_t)sample->pulseHz);
  frame[4] = sample->voltage;
  frame[5] = sample->fuelTemp;
  frame[6] = sample->status;
  jh_store_be32(frame + 7, sample->rawHz);
  jh_store_be32(frame + 11, sample->filteredHz);
  jh_store_be32(frame + 15, sample->baselineHz);
  jh_store_be32(frame + 19, sample->number);
  jh_store_be32(frame + 23, sample->measuredUs);
  jh_store_be16(frame + 27, sample->ageUs);
  frame[29] = sequence;
}

/** @brief Decode one frame. Both pointers non-NULL; errors leave sample
 * unchanged.
 * @return HAL_OK, HAL_EINVAL, HAL_EUNSUPPORTED for version, or HAL_EAGAIN for
 * mixed publication. Caller separately validates status, age and progression of
 * the sample number. */
static inline hal_status_t
adjustometer_feedback_decode(const uint8_t *frame,
                             adjustometer_feedback_t *sample) {
  if (frame == NULL || sample == NULL) {
    return HAL_EINVAL;
  }
  if (frame[0] != ADJUSTOMETER_FEEDBACK_VERSION) {
    return HAL_EUNSUPPORTED;
  }
  if ((frame[1] & 1U) != 0U || frame[1] != frame[29]) {
    return HAL_EAGAIN;
  }
  adjustometer_feedback_t decoded;
  decoded.pulseHz = (int16_t)jh_load_be16(frame + 2);
  decoded.voltage = frame[4];
  decoded.fuelTemp = frame[5];
  decoded.status = frame[6];
  decoded.rawHz = jh_load_be32(frame + 7);
  decoded.filteredHz = jh_load_be32(frame + 11);
  decoded.baselineHz = jh_load_be32(frame + 15);
  decoded.number = jh_load_be32(frame + 19);
  decoded.measuredUs = jh_load_be32(frame + 23);
  decoded.ageUs = jh_load_be16(frame + 27);
  *sample = decoded;
  return HAL_OK;
}
