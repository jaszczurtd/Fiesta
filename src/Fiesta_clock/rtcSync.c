#include "rtcSync.h"

#include "clockTime.h"
#include "config.h"

#include "../common/canDefinitions/canDefinitions.h"

#include <JaszczurHAL.h>
#include <string.h>

typedef struct {
  uint32_t lastTickMs;
  bool frameReceived;
  bool frameValid;
  uint32_t frameEpoch;
  bool correctionHeld;
  uint32_t correctionStartMs;
  bool retryHeld;
  uint32_t retryStartMs;
} rtc_sync_state_t;

static rtc_sync_state_t s_rtcSync;

void rtcSyncInit(uint32_t nowMs) {
  memset(&s_rtcSync, 0, sizeof(s_rtcSync));
  s_rtcSync.lastTickMs = nowMs;
}

void rtcSyncReceive(const uint8_t *data, uint8_t len) {
  if (data == NULL || len <= CAN_FRAME_GPS_UTC_STATUS) {
    derr_limited("rtc", "Truncated GPS UTC frame, len: %u", (unsigned)len);
    return;
  }
  const uint32_t epoch = jh_load_be32(&data[CAN_FRAME_GPS_UTC_EPOCH_B3]);
  s_rtcSync.frameReceived = true;
  s_rtcSync.frameValid =
      (data[CAN_FRAME_GPS_UTC_STATUS] & CAN_GPS_UTC_STATUS_VALID) != 0u &&
      clockTimeEpochInRange(epoch);
  s_rtcSync.frameEpoch = epoch;
}

static void rtcSyncRelease(bool *held, uint32_t startMs, uint32_t periodMs,
                           uint32_t nowMs) {
  if (*held && hal_elapsed_u32(nowMs, startMs, periodMs)) {
    *held = false;
  }
}

/* A failed RTC or KV access stops further attempts for the retry time. */
static bool rtcSyncFailed(hal_status_t status, uint32_t nowMs) {
  if (status == HAL_OK) {
    return false;
  }
  s_rtcSync.retryHeld = true;
  s_rtcSync.retryStartMs = nowMs;
  derr_limited("rtc", "RTC not set to GPS UTC: %s",
               hal_status_to_string(status));
  return true;
}

static void rtcSyncApply(uint32_t gpsEpoch, uint32_t nowMs) {
  uint32_t rtcEpoch = 0u;
  bool trusted = false;
  hal_status_t status = clockTimeReadUtc(&rtcEpoch, &trusted);
  if (status == HAL_OK && trusted) {
    const uint32_t deviation =
        rtcEpoch > gpsEpoch ? rtcEpoch - gpsEpoch : gpsEpoch - rtcEpoch;
    if (deviation <= CFG_RTC_SYNC_TOLERANCE_S || s_rtcSync.correctionHeld) {
      return;
    }
  }
  if (status == HAL_OK) {
    status = clockTimeWriteUtc(gpsEpoch);
  }
  if (rtcSyncFailed(status, nowMs)) {
    return;
  }
  s_rtcSync.correctionHeld = true;
  s_rtcSync.correctionStartMs = nowMs;
  deb("RTC set from GPS UTC %lu", (unsigned long)gpsEpoch);
}

void rtcSyncTick(uint32_t nowMs, bool timeEditActive) {
  const bool queueFresh = !hal_elapsed_u32(nowMs, s_rtcSync.lastTickMs,
                                           CFG_RTC_SYNC_MAX_RX_GAP_MS + 1u);
  s_rtcSync.lastTickMs = nowMs;
  rtcSyncRelease(&s_rtcSync.correctionHeld, s_rtcSync.correctionStartMs,
                 CFG_RTC_SYNC_CORRECTION_INTERVAL_MS, nowMs);
  rtcSyncRelease(&s_rtcSync.retryHeld, s_rtcSync.retryStartMs,
                 CFG_RTC_SYNC_RETRY_MS, nowMs);
  /* An unfinished change to UTC needs no new frame. */
  if (!s_rtcSync.retryHeld && !clockTimeSettled()) {
    (void)rtcSyncFailed(clockTimeSettle(), nowMs);
  }

  const bool use = s_rtcSync.frameReceived && s_rtcSync.frameValid &&
                   queueFresh && !timeEditActive && !s_rtcSync.retryHeld;
  s_rtcSync.frameReceived = false;
  if (use) {
    rtcSyncApply(s_rtcSync.frameEpoch, nowMs);
  }
}
