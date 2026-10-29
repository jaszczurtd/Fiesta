#include "gps.h"
#include "../common/canDefinitions/canDefinitions.h"
#include "config.h"

#include <JaszczurHAL.h>
#include <hal/gps/hal_gps.h>
#include <hal/time/hal_time.h>
#include <math.h>
#include <string.h>

static hal_gps_utc_t s_cachedUtc;
static uint32_t s_cachedEpoch;
// Valid NMEA sentences counted by HAL and when that count last grew.
static uint32_t s_sentences;
static uint32_t s_sentenceMs;
static bool s_sentenceSeen;

void initGPS(void) {
  s_cachedUtc = {};
  s_cachedEpoch = 0u;
  s_sentenceMs = 0u;
  s_sentenceSeen = false;
  hal_gps_init(JH_HW_NODE_GPS_PIN_RX, JH_HW_NODE_GPS_PIN_TX,
               JH_HW_NODE_GPS_PROP_BAUD_RATE, HAL_UART_CFG_8N1);
  s_sentences = hal_gps_passed_checksum();
}

// The RMC calendar changes at receiver rate; polling only advances its age.
static uint32_t rmcEpoch(const hal_gps_utc_t &utc) {
  if (utc.year != s_cachedUtc.year || utc.month != s_cachedUtc.month ||
      utc.day != s_cachedUtc.day || utc.hour != s_cachedUtc.hour ||
      utc.minute != s_cachedUtc.minute || utc.second != s_cachedUtc.second) {
    s_cachedUtc = utc;
    s_cachedEpoch = hal_time_from_components(utc.year, utc.month, utc.day,
                                             utc.hour, utc.minute, utc.second);
  }
  return s_cachedEpoch;
}

void clocksGpsExpire(clocks_gps_snapshot_t *gps, uint32_t nowMs) {
  if (gps == nullptr)
    return;
  if (hal_elapsed_u32(nowMs, gps->locationUpdatedMs, GPS_MAX_AGE_MS + 1u))
    gps->available = false;
  if (!gps->available ||
      hal_elapsed_u32(nowMs, gps->utcUpdatedMs, GPS_UTC_MAX_AGE_MS + 1u)) {
    gps->utcValid = false;
    gps->epochUtc = 0u;
  }
  if (!gps->available) {
    gps->latitude = gps->longitude = gps->speedKmh = 0.0;
    if (hal_elapsed_u32(nowMs, gps->linkUpdatedMs, GPS_LINK_MAX_AGE_MS + 1u))
      gps->linked = false;
  }
}

bool readGPS(clocks_gps_snapshot_t *out) {
  if (out == nullptr)
    return false;
  hal_gps_update();
  const bool updated = hal_gps_location_is_updated();
  *out = {};
  const uint32_t nowMs = hal_millis();
  // Checksum-valid sentences, not raw bytes: noise or a wrong baud rate is no
  // communication.
  const uint32_t sentences = hal_gps_passed_checksum();
  if (sentences != s_sentences) {
    s_sentences = sentences;
    s_sentenceMs = nowMs;
    s_sentenceSeen = true;
  }
  const uint32_t age = hal_gps_location_age();
  out->latitude = hal_gps_latitude();
  out->longitude = hal_gps_longitude();
  out->speedKmh = hal_gps_speed_kmph();
  out->available = hal_gps_location_is_valid() && isfinite(out->latitude) &&
                   fabs(out->latitude) <= 90.0 && isfinite(out->longitude) &&
                   fabs(out->longitude) <= 180.0;
  out->locationUpdatedMs = nowMs - age;
  if (!isfinite(out->speedKmh) || out->speedKmh < GPS_MIN_SPEED_KMH)
    out->speedKmh = 0.0;
  hal_gps_utc_t utc;
  if (hal_gps_get_utc(&utc) == HAL_OK) {
    out->epochUtc = rmcEpoch(utc);
    out->utcValid = out->epochUtc != 0u;
    out->utcUpdatedMs = nowMs - utc.age_ms;
  }
  out->linked = s_sentenceSeen;
  out->linkUpdatedMs = s_sentenceMs;
  // A valid fix proves the link, also where HAL counts no sentence; an older
  // fix never moves the time of a later sentence back.
  if (out->available && (!out->linked || (int32_t)(out->locationUpdatedMs -
                                                   out->linkUpdatedMs) > 0)) {
    out->linked = true;
    out->linkUpdatedMs = out->locationUpdatedMs;
  }
  clocksGpsExpire(out, nowMs);
  return updated;
}

bool clocksGpsReadChanged(clocks_gps_snapshot_t *gps) {
  if (gps == nullptr)
    return false;
  const bool available = gps->available;
  const bool utcValid = gps->utcValid;
  const bool linked = gps->linked;
  const double speed = gps->speedKmh;
  const bool updated = readGPS(gps);
  return updated || available != gps->available || utcValid != gps->utcValid ||
         linked != gps->linked || speed != gps->speedKmh;
}

int32_t clocksGpsMicrodegrees(double degrees) {
  if (!isfinite(degrees) || fabs(degrees) > 180.0)
    return 0;
  return (int32_t)lround(degrees * 1000000.0);
}

uint16_t clocksGpsSpeedX10(const clocks_gps_snapshot_t *gps) {
  if (gps == nullptr || !gps->available || !isfinite(gps->speedKmh) ||
      gps->speedKmh < GPS_MIN_SPEED_KMH)
    return 0u;
  const double scaled = gps->speedKmh * 10.0;
  return scaled >= (double)UINT16_MAX ? UINT16_MAX : (uint16_t)lround(scaled);
}

hal_status_t clocksGpsEncode(const clocks_gps_snapshot_t *gps, uint32_t nowMs,
                             uint8_t *lat, uint8_t *lonTime, uint8_t *utc) {
  if (gps == nullptr || lat == nullptr || lonTime == nullptr || utc == nullptr)
    return HAL_EINVAL;
  memset(lat, 0, CAN_FRAME_MAX_LENGTH);
  memset(lonTime, 0, CAN_FRAME_MAX_LENGTH);
  memset(utc, 0, CAN_FRAME_MAX_LENGTH);
  if (gps->available) {
    jh_store_be32(lat + CAN_FRAME_GPS_EXT_LAT_B3,
                  (uint32_t)clocksGpsMicrodegrees(gps->latitude));
    jh_store_be32(lonTime + CAN_FRAME_GPS_EXT_LON_B3,
                  (uint32_t)clocksGpsMicrodegrees(gps->longitude));
    lat[CAN_FRAME_GPS_EXT_STATUS] = CAN_GPS_STATUS_AVAILABLE;
    jh_store_be16(lat + CAN_FRAME_GPS_EXT_SPEED_HI, clocksGpsSpeedX10(gps));
  }
  if (gps->utcValid) {
    // The sentence may be up to GPS_UTC_MAX_AGE_MS old; the frames carry the
    // time of sending.
    const uint32_t epoch =
        gps->epochUtc + (nowMs - gps->utcUpdatedMs + 500u) / 1000u;
    jh_store_be32(utc + CAN_FRAME_GPS_UTC_EPOCH_B3, epoch);
    utc[CAN_FRAME_GPS_UTC_STATUS] = CAN_GPS_UTC_STATUS_VALID;
    (void)fiesta_can_put_utc_datetime(&lonTime[CAN_FRAME_GPS_EXT_DT_HI], epoch);
  }
  return HAL_OK;
}
