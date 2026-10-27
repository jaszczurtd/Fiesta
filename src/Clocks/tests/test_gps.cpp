#include "clocksState.h"
#include "config.h"
#include "gps.h"
#include <cmath>
#include <cstring>
#include <hal/gps/hal_gps.h>
#include <hal/impl/.mock/hal_mock.h>
#include <hal/time/hal_time.h>
#include <unity.h>

void setUp(void) {
  hal_mock_set_millis(10000u);
  initGPS();
  clocksStateInit();
  const clocks_local_snapshot_t empty = {};
  clocksStatePublishLocal(&empty);
}
void tearDown(void) {}

static void fix(void) {
  hal_mock_gps_set_location(52.2296756, -123.1234566);
  hal_mock_gps_set_valid(true);
  hal_mock_gps_set_age(0u);
  hal_mock_gps_set_speed(15.06);
  const hal_gps_utc_t utc = {2026u, 10u, 26u, 12u, 34u, 56u, 0u};
  hal_mock_gps_set_utc(&utc);
}

// Feed the production HAL parser; checksum construction only, no NMEA parser
// here.
static void sentence(const char *body) {
  uint8_t checksum = 0u;
  hal_gps_encode('$');
  for (const char *c = body; *c; ++c) {
    checksum ^= (uint8_t)*c;
    hal_gps_encode(*c);
  }
  const char hex[] = "0123456789ABCDEF";
  hal_gps_encode('*');
  hal_gps_encode(hex[checksum >> 4]);
  hal_gps_encode(hex[checksum & 15u]);
  hal_gps_encode('\r');
  hal_gps_encode('\n');
}

void test_missing_receiver_and_no_fix_are_unavailable(void) {
  clocks_gps_snapshot_t gps;
  readGPS(&gps);
  TEST_ASSERT_FALSE(gps.available);
  TEST_ASSERT_FALSE(gps.utcValid);
  sentence("GPRMC,123456,V,,,,,,,261026,,,N");
  readGPS(&gps);
  TEST_ASSERT_FALSE(gps.available);
  TEST_ASSERT_EQUAL_UINT32(0u, gps.epochUtc);
}

void test_position_retains_double_precision_and_utc_seconds(void) {
  fix();
  clocks_gps_snapshot_t gps;
  readGPS(&gps);
  TEST_ASSERT_TRUE(gps.available);
  TEST_ASSERT_TRUE(gps.utcValid);
  TEST_ASSERT_EQUAL_UINT32(1793018096u, gps.epochUtc);
  TEST_ASSERT_EQUAL_INT32(52229676, clocksGpsMicrodegrees(gps.latitude));
  TEST_ASSERT_EQUAL_INT32(-123123457, clocksGpsMicrodegrees(gps.longitude));
  TEST_ASSERT_EQUAL_UINT16(151u, clocksGpsSpeedX10(&gps));
}

void test_invalid_location_clears_even_fresh_utc(void) {
  fix();
  hal_mock_gps_set_valid(false);
  hal_gps_utc_t utc;
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_gps_get_utc(&utc));
  TEST_ASSERT_EQUAL_UINT32(0u, utc.age_ms);
  clocks_gps_snapshot_t gps;
  readGPS(&gps);
  TEST_ASSERT_FALSE(gps.available);
  TEST_ASSERT_FALSE(gps.utcValid);
  TEST_ASSERT_EQUAL_UINT32(0u, gps.epochUtc);
  TEST_ASSERT_TRUE(gps.latitude == 0.0 && gps.longitude == 0.0 &&
                   gps.speedKmh == 0.0);
}

void test_gga_time_cannot_replace_the_date_time_pair_from_rmc(void) {
  sentence("GPRMC,233000,A,4807.038,N,01131.000,E,022.4,084.4,311226,003.1,W");
  sentence("GPGGA,000001,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  clocks_gps_snapshot_t gps;
  readGPS(&gps);
  TEST_ASSERT_TRUE(gps.utcValid);
  TEST_ASSERT_EQUAL_UINT32(1798759800u, gps.epochUtc);
}

void test_location_and_utc_expire_at_their_own_age_limits(void) {
  fix();
  clocks_gps_snapshot_t gps;
  hal_mock_gps_set_age(GPS_MAX_AGE_MS);
  const hal_gps_utc_t utc = {
      2026u, 10u, 26u, 12u, 34u, 56u, GPS_UTC_MAX_AGE_MS};
  hal_mock_gps_set_utc(&utc);
  readGPS(&gps);
  TEST_ASSERT_TRUE(gps.available);
  TEST_ASSERT_TRUE(gps.utcValid);
  clocksGpsExpire(&gps, hal_millis() + 1u);
  TEST_ASSERT_FALSE(gps.available);
  TEST_ASSERT_FALSE(gps.utcValid);
  fix();
  readGPS(&gps);
  clocksGpsExpire(&gps, hal_millis() + GPS_UTC_MAX_AGE_MS + 1u);
  TEST_ASSERT_TRUE(gps.available);
  TEST_ASSERT_FALSE(gps.utcValid);
  TEST_ASSERT_EQUAL_UINT32(0u, gps.epochUtc);
}

void test_missing_new_fixes_expire_and_a_new_rmc_restores_them(void) {
  sentence("GPRMC,123456,A,4807.038,N,01131.000,E,022.4,084.4,261026,003.1,W");
  clocks_gps_snapshot_t gps;
  readGPS(&gps);
  TEST_ASSERT_TRUE(gps.available);
  sentence("GPRMC,123457,V,,,,,,,261026,,,N");
  hal_mock_set_millis(10000u + GPS_MAX_AGE_MS + 1u);
  readGPS(&gps);
  TEST_ASSERT_FALSE(gps.available);
  TEST_ASSERT_FALSE(gps.utcValid);
  sentence("GPRMC,123502,A,4807.038,N,01131.000,E,022.4,084.4,261026,003.1,W");
  readGPS(&gps);
  TEST_ASSERT_TRUE(gps.available);
  TEST_ASSERT_TRUE(gps.utcValid);
  TEST_ASSERT_EQUAL_UINT32(1793018102u, gps.epochUtc);
}

void test_published_gps_expires_when_core_one_stops_and_across_tick_wrap(void) {
  hal_mock_set_millis(UINT32_MAX - 100u);
  fix();
  clocks_local_snapshot_t local = {};
  readGPS(&local.gps);
  clocksStatePublishLocal(&local);
  hal_mock_set_millis((uint32_t)(UINT32_MAX - 100u + GPS_UTC_MAX_AGE_MS + 1u));
  clocksStateCopyLocal(&local);
  TEST_ASSERT_TRUE(local.gps.available);
  TEST_ASSERT_FALSE(local.gps.utcValid);
  hal_mock_set_millis((uint32_t)(UINT32_MAX - 100u + GPS_MAX_AGE_MS + 1u));
  clocksStateCopyLocal(&local);
  TEST_ASSERT_FALSE(local.gps.available);
  TEST_ASSERT_EQUAL_INT32(0, clocksGpsMicrodegrees(local.gps.latitude));
}

void test_can_frames_encode_precise_coordinates_speed_and_utc(void) {
  fix();
  clocks_gps_snapshot_t gps;
  readGPS(&gps);
  uint8_t lat[8], lon[8], utc[8];
  TEST_ASSERT_EQUAL_INT(HAL_OK, clocksGpsEncode(&gps, lat, lon, utc));
  TEST_ASSERT_EQUAL_INT32(52229676, (int32_t)jh_load_be32(lat + 1));
  TEST_ASSERT_EQUAL_INT32(-123123457, (int32_t)jh_load_be32(lon + 1));
  TEST_ASSERT_EQUAL_UINT8(1u, lat[5]);
  TEST_ASSERT_EQUAL_UINT16(151u, jh_load_be16(lat + 6));
  const uint8_t calendar[] = {0x6au, 0xd3u, 0x22u};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(calendar, lon + 5, 3u);
  TEST_ASSERT_EQUAL_UINT32(1793018096u, jh_load_be32(utc + 1));
  TEST_ASSERT_EQUAL_HEX8(1u, utc[5]);
  TEST_ASSERT_EQUAL_HEX8(0u, utc[6] | utc[7]);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, clocksGpsEncode(nullptr, lat, lon, utc));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, clocksGpsEncode(&gps, nullptr, lon, utc));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, clocksGpsEncode(&gps, lat, nullptr, utc));
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, clocksGpsEncode(&gps, lat, lon, nullptr));
  gps.available = gps.utcValid = false;
  TEST_ASSERT_EQUAL_INT(HAL_OK, clocksGpsEncode(&gps, lat, lon, utc));
  const uint8_t zero[8] = {};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, lat, 8u);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, lon, 8u);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, utc, 8u);
}

void test_speed_threshold_and_wire_limits(void) {
  clocks_gps_snapshot_t gps = {};
  gps.available = true;
  gps.speedKmh = 4.999;
  TEST_ASSERT_EQUAL_UINT16(0u, clocksGpsSpeedX10(&gps));
  gps.speedKmh = 5.0;
  TEST_ASSERT_EQUAL_UINT16(50u, clocksGpsSpeedX10(&gps));
  gps.speedKmh = 1e20;
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, clocksGpsSpeedX10(&gps));
  gps.speedKmh = NAN;
  TEST_ASSERT_EQUAL_UINT16(0u, clocksGpsSpeedX10(&gps));
  TEST_ASSERT_EQUAL_INT32(0, clocksGpsMicrodegrees(NAN));
  TEST_ASSERT_EQUAL_INT32(0, clocksGpsMicrodegrees(INFINITY));
  TEST_ASSERT_EQUAL_INT32(180000000, clocksGpsMicrodegrees(180.0));
  TEST_ASSERT_EQUAL_INT32(-180000000, clocksGpsMicrodegrees(-180.0));
  TEST_ASSERT_EQUAL_INT32(0, clocksGpsMicrodegrees(180.000001));
  TEST_ASSERT_EQUAL_INT32(0, clocksGpsMicrodegrees(-200.0));
}

void test_legacy_year_does_not_wrap_or_change_utc(void) {
  clocks_gps_snapshot_t gps = {};
  gps.available = gps.utcValid = true;
  gps.epochUtc = 2082758400u;
  uint8_t lat[8], lon[8], utc[8];
  TEST_ASSERT_EQUAL_INT(HAL_OK, clocksGpsEncode(&gps, lat, lon, utc));
  TEST_ASSERT_EQUAL_HEX8(0u, lon[5] | lon[6] | lon[7]);
  TEST_ASSERT_EQUAL_UINT32(2082758400u, jh_load_be32(utc + 1));
}

void test_publication_poll_renews_unchanged_fixes_without_idle_updates(void) {
  clocks_gps_snapshot_t gps = {};
  TEST_ASSERT_FALSE(clocksGpsReadChanged(nullptr));
  TEST_ASSERT_FALSE(clocksGpsReadChanged(&gps));
  sentence("GPRMC,123456,A,4807.038,N,01131.000,E,022.4,084.4,261026,003.1,W");
  TEST_ASSERT_TRUE(clocksGpsReadChanged(&gps));
  TEST_ASSERT_FALSE(clocksGpsReadChanged(&gps));
  hal_mock_set_millis(11000u);
  sentence("GPRMC,123457,A,4807.038,N,01131.000,E,022.4,084.4,261026,003.1,W");
  TEST_ASSERT_TRUE(clocksGpsReadChanged(&gps));
  TEST_ASSERT_EQUAL_UINT32(11000u, gps.locationUpdatedMs);
  TEST_ASSERT_EQUAL_UINT32(1793018097u, gps.epochUtc);
  TEST_ASSERT_FALSE(clocksGpsReadChanged(&gps));
}

void test_publication_poll_reports_changes_without_a_new_fix(void) {
  fix();
  hal_mock_gps_set_updated(false);
  clocks_gps_snapshot_t gps = {};
  TEST_ASSERT_TRUE(clocksGpsReadChanged(&gps));
  TEST_ASSERT_FALSE(clocksGpsReadChanged(&gps));
  hal_mock_gps_set_speed(20.0);
  TEST_ASSERT_TRUE(clocksGpsReadChanged(&gps));
  TEST_ASSERT_FALSE(clocksGpsReadChanged(&gps));
  const hal_gps_utc_t stale = {
      2026u, 10u, 26u, 12u, 34u, 56u, GPS_UTC_MAX_AGE_MS + 1u};
  hal_mock_gps_set_utc(&stale);
  TEST_ASSERT_TRUE(clocksGpsReadChanged(&gps));
  TEST_ASSERT_TRUE(gps.available);
  TEST_ASSERT_FALSE(gps.utcValid);
  TEST_ASSERT_FALSE(clocksGpsReadChanged(&gps));
  hal_gps_utc_t fresh = stale;
  fresh.age_ms = 0u;
  hal_mock_gps_set_utc(&fresh);
  TEST_ASSERT_TRUE(clocksGpsReadChanged(&gps));
  TEST_ASSERT_TRUE(gps.utcValid);
  TEST_ASSERT_FALSE(clocksGpsReadChanged(&gps));
}

void test_publication_poll_detects_availability_without_utc_or_speed(void) {
  fix();
  hal_mock_gps_set_updated(false);
  hal_mock_gps_set_speed(0.0);
  const hal_gps_utc_t stale = {
      2026u, 10u, 26u, 12u, 34u, 56u, GPS_UTC_MAX_AGE_MS + 1u};
  hal_mock_gps_set_utc(&stale);
  clocks_gps_snapshot_t gps = {};
  TEST_ASSERT_TRUE(clocksGpsReadChanged(&gps));
  TEST_ASSERT_FALSE(gps.utcValid);
  TEST_ASSERT_FALSE(clocksGpsReadChanged(&gps));
  hal_mock_gps_set_age(GPS_MAX_AGE_MS + 1u);
  TEST_ASSERT_TRUE(clocksGpsReadChanged(&gps));
  TEST_ASSERT_FALSE(gps.available);
  TEST_ASSERT_FALSE(clocksGpsReadChanged(&gps));
  hal_mock_gps_set_age(0u);
  TEST_ASSERT_TRUE(clocksGpsReadChanged(&gps));
  hal_mock_gps_set_valid(false);
  TEST_ASSERT_TRUE(clocksGpsReadChanged(&gps));
  TEST_ASSERT_FALSE(gps.available);
  TEST_ASSERT_FALSE(clocksGpsReadChanged(&gps));
  hal_mock_gps_set_valid(true);
  TEST_ASSERT_TRUE(clocksGpsReadChanged(&gps));
  TEST_ASSERT_TRUE(gps.available);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_missing_receiver_and_no_fix_are_unavailable);
  RUN_TEST(test_position_retains_double_precision_and_utc_seconds);
  RUN_TEST(test_invalid_location_clears_even_fresh_utc);
  RUN_TEST(test_gga_time_cannot_replace_the_date_time_pair_from_rmc);
  RUN_TEST(test_location_and_utc_expire_at_their_own_age_limits);
  RUN_TEST(test_missing_new_fixes_expire_and_a_new_rmc_restores_them);
  RUN_TEST(test_published_gps_expires_when_core_one_stops_and_across_tick_wrap);
  RUN_TEST(test_can_frames_encode_precise_coordinates_speed_and_utc);
  RUN_TEST(test_speed_threshold_and_wire_limits);
  RUN_TEST(test_legacy_year_does_not_wrap_or_change_utc);
  RUN_TEST(test_publication_poll_renews_unchanged_fixes_without_idle_updates);
  RUN_TEST(test_publication_poll_reports_changes_without_a_new_fix);
  RUN_TEST(test_publication_poll_detects_availability_without_utc_or_speed);
  return UNITY_END();
}
