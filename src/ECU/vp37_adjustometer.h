#ifndef VP37_ADJUSTOMETER_H
#define VP37_ADJUSTOMETER_H

/**
 * @file vp37_adjustometer.h
 * @brief What the VP37 module keeps of its Adjustometer: the last reading and
 * the reader state. The frame layout and decoder live in
 * adjustometer_feedback.h; the bus transfer is a board service.
 */

#include "../common/adjustometer_feedback.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One Adjustometer reading; failed reads keep the previous values. */
typedef struct {
  int16_t pulseHz;             /**< Deviation from the baseline [Hz]. */
  uint8_t voltageRaw;          /**< Supply voltage in 0.1 V units. */
  uint8_t fuelTempC;           /**< Fuel temperature [C]. */
  uint8_t status;              /**< Bitmask of ADJ_STATUS_*. */
  bool commOk;                 /**< The last transfer succeeded. */
  uint32_t signalHz;           /**< Filtered absolute frequency [Hz]. */
  uint32_t baselineHz;         /**< Locked zero reference [Hz]. */
  int32_t signedDeltaHz;       /**< signalHz - baselineHz, no zero hold. */
  int16_t chipTempDeciC;       /**< Adjustometer die temperature [0.1 C]. */
  uint8_t extendedFlags;       /**< Bitmask of ADJUSTOMETER_EXT_FLAG_*. */
  bool extendedTelemetryValid; /**< The versioned extension was coherent. */
  bool fastFeedback;           /**< Read from the versioned fast frame. */
  bool feedbackFresh;          /**< The fast frame advanced in time. */
  uint32_t rawHz, sampleNumber, measuredUs;
  uint16_t ageUs;
  hal_status_t readStatus; /**< Result of the last transfer and decode. */
  uint32_t readUs;         /**< Duration of the last read [us]. */
  uint8_t readRetries;     /**< Repeated transfers after a torn frame. */
} adjustometer_reading_t;

/** @brief Reader state kept in the pump. */
typedef struct {
  adjustometer_reading_t last; /**< Newest reading, returned on failure. */
  uint8_t commErrors;          /**< Failed legacy reads in a row. */
  bool fastFeedback;           /**< Versioned fast frame selected. */
  bool sampleTracked;          /**< A fast sample number has been seen. */
  uint32_t sampleChangedUs;    /**< When the sample number last advanced. */
} VP37AdjustometerReader;

#ifdef __cplusplus
}
#endif

#endif
