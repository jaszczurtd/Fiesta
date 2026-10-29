#ifndef C_GPS
#define C_GPS

#include <hal/core/hal_status.h>
#include <stdbool.h>
#include <stdint.h>

/** @brief GPS values sampled on core 1; coordinates retain HAL precision. */
typedef struct {
  double latitude;
  double longitude;
  double speedKmh;
  uint32_t locationUpdatedMs;
  uint32_t utcUpdatedMs;
  uint32_t linkUpdatedMs; // last valid NMEA sentence or fix
  uint32_t epochUtc;
  bool available;
  bool utcValid;
  // The receiver communicates: a valid sentence within GPS_LINK_MAX_AGE_MS
  // or a valid fix. False with a missing receiver or a broken RX line.
  bool linked;
} clocks_gps_snapshot_t;

/** @brief Core 1: initialize the PIO/software-serial receiver without waiting
 * for data. */
void initGPS(void);
/**
 * @brief Core 1: poll HAL and replace the snapshot; NULL is ignored.
 * @return True when HAL reports a new location (also for an unchanged fix).
 *         Availability can expire without a new location.
 */
bool readGPS(clocks_gps_snapshot_t *out);
/**
 * @brief Core 1: poll HAL, replacing the previous snapshot in place.
 * @param gps In/out snapshot; NULL is ignored and returns false.
 * @return True when a new fix, including an unchanged position, or a change
 *         of availability, UTC validity, receiver link or speed requires
 *         publication.
 */
bool clocksGpsReadChanged(clocks_gps_snapshot_t *gps);
/** @brief Expire location, UTC and the receiver link independently at nowMs;
 * NULL is ignored. A valid location keeps the link. */
void clocksGpsExpire(clocks_gps_snapshot_t *gps, uint32_t nowMs);
/** @brief Round finite degrees to signed microdegrees; invalid input yields 0.
 */
int32_t clocksGpsMicrodegrees(double degrees);
/** @brief Speed in tenths of km/h, zero below 5 km/h, capped to uint16_t. */
uint16_t clocksGpsSpeedX10(const clocks_gps_snapshot_t *gps);
/**
 * @brief Encode GPS 0x12D/0x12E and UTC 0x131, leaving frame numbers zero.
 * @param gps Snapshot already checked for freshness.
 * @param nowMs hal_millis() of sending; the UTC of the sentence moves on by
 *        its age, to the nearest second.
 * @param lat Eight-byte 0x12D payload.
 * @param lonTime Eight-byte 0x12E payload, date/time in UTC.
 * @param utc Eight-byte 0x131 payload.
 * @return HAL_OK, HAL_EINVAL for any NULL argument.
 */
hal_status_t clocksGpsEncode(const clocks_gps_snapshot_t *gps, uint32_t nowMs,
                             uint8_t *lat, uint8_t *lonTime, uint8_t *utc);

#endif
