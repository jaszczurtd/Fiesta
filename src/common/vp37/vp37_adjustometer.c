// VP37 Adjustometer reader: the fast feedback frame, the legacy register
// block, the diagnostic extension and the start-up baseline wait. The bus
// transfer is the board's adjustometerTransfer service; everything the frames
// mean is decided here.

#include "vp37_internal.h"

#include <hal/core/jh_endian.h>
#include <string.h>

/** Failed legacy reads in a row before commOk drops. */
#define VP37_ADJ_COMM_ERROR_THRESHOLD 3U
/** Transfers of a fast frame that arrived torn between two publications. */
#define VP37_ADJ_FEEDBACK_ATTEMPTS 3U
/** Transfers of the diagnostic extension before it is given up. */
#define VP37_ADJ_EXTENDED_ATTEMPTS 2U
/** Pause between two baseline polls [ms]. */
#define VP37_ADJ_BASELINE_POLL_MS 10U

hal_status_t VP37_setAdjustometerFastFeedback(VP37Pump *self, bool enabled) {
  hal_status_t status = HAL_EINVAL;
  if (self != NULL) {
    status = HAL_ESTATE;
    if (!self->vp37Initialized && !self->servicesFixed) {
      (void)memset(&self->adjustometer, 0, sizeof(self->adjustometer));
      self->adjustometer.last.status = ADJ_STATUS_SIGNAL_LOST;
      self->adjustometer.last.readStatus = HAL_EAGAIN;
      self->adjustometer.fastFeedback = enabled;
      status = HAL_OK;
    }
  }
  return status;
}

static adjustometer_reading_t VP37_adjustometerCommError(VP37Pump *self) {
  self->adjustometer.commErrors++;
  if (self->adjustometer.commErrors >= VP37_ADJ_COMM_ERROR_THRESHOLD) {
    self->adjustometer.last.commOk = false;
  }
  return self->adjustometer.last;
}

static adjustometer_reading_t VP37_readFastFeedback(VP37Pump *self) {
  const uint32_t startedUs = hal_micros();
  uint8_t retries = 0U;
  uint8_t frame[ADJUSTOMETER_FEEDBACK_BYTES];
  adjustometer_feedback_t decoded = {0};
  hal_status_t status = HAL_EBUS;
  bool tornFrame = true;
  for (uint8_t attempt = 0U;
       (attempt < VP37_ADJ_FEEDBACK_ATTEMPTS) && tornFrame; attempt++) {
    retries = attempt;
    status = self->callbacks.adjustometerTransfer(ADJUSTOMETER_FEEDBACK_START,
                                                  frame, COUNTOF(frame));
    tornFrame = false;
    if (status == HAL_OK) {
      status = adjustometer_feedback_decode(frame, &decoded);
      tornFrame = (status == HAL_EAGAIN);
    }
  }
  const uint32_t nowUs = hal_micros();
  adjustometer_reading_t snapshot = self->adjustometer.last;
  snapshot.fastFeedback = true;
  snapshot.readStatus = status;
  snapshot.readUs = nowUs - startedUs;
  snapshot.readRetries = retries;
  snapshot.feedbackFresh = false;
  snapshot.commOk = status == HAL_OK;
  if (status == HAL_OK) {
    const bool tracked = self->adjustometer.sampleTracked;
    const bool advanced = !tracked || (decoded.number != snapshot.sampleNumber);
    const bool clockBackwards =
        tracked && ((decoded.measuredUs - snapshot.measuredUs) >= 0x80000000U);
    if (advanced) {
      self->adjustometer.sampleChangedUs = nowUs;
    }
    snapshot.feedbackFresh =
        !clockBackwards &&
        (decoded.ageUs <= ADJUSTOMETER_FEEDBACK_MAX_AGE_US) &&
        !hal_elapsed_u32(nowUs, self->adjustometer.sampleChangedUs,
                         ADJUSTOMETER_FEEDBACK_MAX_AGE_US);
    self->adjustometer.sampleTracked = true;
    snapshot.pulseHz = decoded.pulseHz;
    snapshot.voltageRaw = decoded.voltage;
    snapshot.fuelTempC = decoded.fuelTemp;
    snapshot.status = decoded.status;
    snapshot.rawHz = decoded.rawHz;
    snapshot.signalHz = decoded.filteredHz;
    snapshot.baselineHz = decoded.baselineHz;
    snapshot.signedDeltaHz =
        (int32_t)decoded.filteredHz - (int32_t)decoded.baselineHz;
    snapshot.sampleNumber = decoded.number;
    snapshot.measuredUs = decoded.measuredUs;
    snapshot.ageUs = decoded.ageUs;
  }
  self->adjustometer.last = snapshot;
  return snapshot;
}

static adjustometer_reading_t VP37_readLegacyRegisters(VP37Pump *self) {
  uint8_t buf[ADJUSTOMETER_LEGACY_REG_COUNT];
  const hal_status_t status = self->callbacks.adjustometerTransfer(
      ADJUSTOMETER_REG_PULSE_HI, buf, COUNTOF(buf));
  if (status != HAL_OK) {
    derr("Adjustometer I2C read error: %s", hal_status_to_string(status));
    return VP37_adjustometerCommError(self);
  }
  adjustometer_reading_t snapshot = self->adjustometer.last;
  snapshot.pulseHz = (int16_t)jh_load_be16(buf);
  snapshot.voltageRaw = buf[2];
  snapshot.fuelTempC = buf[3];
  snapshot.status = buf[4];
  snapshot.commOk = true;
  snapshot.fastFeedback = false;
  self->adjustometer.last = snapshot;
  self->adjustometer.commErrors = 0U;
  return snapshot;
}

adjustometer_reading_t VP37_readAdjustometer(VP37Pump *self) {
  return self->adjustometer.fastFeedback ? VP37_readFastFeedback(self)
                                         : VP37_readLegacyRegisters(self);
}

static bool VP37_readExtendedOnce(const VP37Pump *self,
                                  adjustometer_reading_t *out) {
  uint8_t buf[ADJUSTOMETER_EXT_REG_COUNT];
  if (self->callbacks.adjustometerTransfer(ADJUSTOMETER_EXT_REG_START, buf,
                                           COUNTOF(buf)) != HAL_OK) {
    return false;
  }
  const uint8_t version =
      buf[ADJUSTOMETER_REG_EXT_VERSION - ADJUSTOMETER_EXT_REG_START];
  const uint8_t seqBegin =
      buf[ADJUSTOMETER_REG_EXT_SEQ_BEGIN - ADJUSTOMETER_EXT_REG_START];
  const uint8_t seqEnd =
      buf[ADJUSTOMETER_REG_EXT_SEQ_END - ADJUSTOMETER_EXT_REG_START];
  if ((version != ADJUSTOMETER_EXT_VERSION) || (seqBegin != seqEnd) ||
      ((seqBegin & 1U) != 0U)) {
    return false;
  }
  out->extendedFlags =
      buf[ADJUSTOMETER_REG_EXT_FLAGS - ADJUSTOMETER_EXT_REG_START];
  out->signalHz = jh_load_be32(
      &buf[ADJUSTOMETER_REG_SIGNAL_HZ - ADJUSTOMETER_EXT_REG_START]);
  out->baselineHz = jh_load_be32(
      &buf[ADJUSTOMETER_REG_BASELINE_HZ - ADJUSTOMETER_EXT_REG_START]);
  out->signedDeltaHz = (int32_t)jh_load_be32(
      &buf[ADJUSTOMETER_REG_SIGNED_DELTA_HZ - ADJUSTOMETER_EXT_REG_START]);
  out->chipTempDeciC = (int16_t)jh_load_be16(
      &buf[ADJUSTOMETER_REG_CHIP_TEMP_DECI_C - ADJUSTOMETER_EXT_REG_START]);
  out->extendedTelemetryValid = true;
  return true;
}

bool VP37_readAdjustometerExtended(const VP37Pump *self,
                                   adjustometer_reading_t *reading) {
  bool received = false;
  if ((self != NULL) && (reading != NULL) &&
      (self->callbacks.adjustometerTransfer != NULL)) {
    for (uint8_t attempt = 0U;
         (attempt < VP37_ADJ_EXTENDED_ATTEMPTS) && !received; attempt++) {
      received = VP37_readExtendedOnce(self, reading);
    }
  }
  return received;
}

bool VP37_waitForAdjustometerBaseline(VP37Pump *self) {
  const uint32_t start = hal_millis();
  while ((hal_millis() - start) < VP37_ADJUSTOMETER_BASELINE_WAIT_MS) {
    const adjustometer_reading_t r = VP37_readAdjustometer(self);
    // A failed transfer means the Adjustometer may still be booting.
    if (r.commOk &&
        ((r.status & (ADJ_STATUS_BASELINE_PENDING | ADJ_STATUS_SIGNAL_LOST)) ==
         0U) &&
        (!r.fastFeedback || r.feedbackFresh)) {
      deb("Adjustometer baseline ready (%lu ms)",
          (unsigned long)(hal_millis() - start));
      return true;
    }
    hal_delay_ms(VP37_ADJ_BASELINE_POLL_MS);
    self->callbacks.feedWatchdog();
  }
  derr("Adjustometer baseline timeout (%u ms)",
       (unsigned)VP37_ADJUSTOMETER_BASELINE_WAIT_MS);
  return false;
}
