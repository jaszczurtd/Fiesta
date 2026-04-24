#include "unity.h"
#include "can.h"
#include "sensors.h"
#include "hal/impl/.mock/hal_mock.h"

static void ensure_can_ready(void) {
    if (canTestGetCanHandle() == NULL) {
        canInit(1);
    }
    TEST_ASSERT_NOT_NULL(canTestGetCanHandle());
    hal_mock_can_reset(canTestGetCanHandle());
}

void setUp(void) {
    initSensors();
    ensure_can_ready();
    for(int32_t i = 0; i < F_LAST; i++) {
        setGlobalValue(i, 0.0f);
    }
}

void tearDown(void) {}

void test_pack_gps_datetime_valid(void) {
    // 2026-04-03 14:27 -> yy=26 => offset=6
    // packed: [yearOff:4][month:4][day:5][hour:5][min:6]
    uint32_t packed = CAN_packGpsDateTime(260403, 1427);
    uint32_t expected = (6u << 20) | (4u << 16) | (3u << 11) | (14u << 6) | 27u;
    TEST_ASSERT_EQUAL_UINT32(expected, packed);
}

void test_pack_gps_datetime_invalid_returns_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, CAN_packGpsDateTime(261303, 1427)); // invalid month
    TEST_ASSERT_EQUAL_UINT32(0u, CAN_packGpsDateTime(260403, 2460)); // invalid time
}

void test_build_gps_lat_lon_frames_encode_high_precision_values(void) {
    setGlobalValue(F_LATITUDE, 52.2297f);
    setGlobalValue(F_LONGITUDE, 21.0122f);
    setGlobalValue(F_GPS_IS_AVAILABLE, 1.0f);
    setGlobalValue(F_GPS_DATE, 260403.0f); // YYMMDD
    setGlobalValue(F_GPS_TIME, 1427.0f);   // HHMM

    uint8_t latBuf[CAN_FRAME_MAX_LENGTH] = {0};
    uint8_t lonBuf[CAN_FRAME_MAX_LENGTH] = {0};
    TEST_ASSERT_TRUE(CAN_buildGpsLatFrame(0x33, latBuf, (int)sizeof(latBuf)));
    TEST_ASSERT_TRUE(CAN_buildGpsLonTimeFrame(0x34, lonBuf, (int)sizeof(lonBuf)));

    TEST_ASSERT_EQUAL_UINT8(0x33, latBuf[CAN_FRAME_NUMBER]);
    TEST_ASSERT_EQUAL_UINT8(0x34, lonBuf[CAN_FRAME_NUMBER]);
    TEST_ASSERT_EQUAL_UINT8(1, latBuf[CAN_FRAME_GPS_EXT_STATUS]);

    int32_t lat = (int32_t)(((uint32_t)latBuf[CAN_FRAME_GPS_EXT_LAT_B3] << 24)
                          | ((uint32_t)latBuf[CAN_FRAME_GPS_EXT_LAT_B2] << 16)
                          | ((uint32_t)latBuf[CAN_FRAME_GPS_EXT_LAT_B1] << 8)
                          | (uint32_t)latBuf[CAN_FRAME_GPS_EXT_LAT_B0]);
    int32_t lon = (int32_t)(((uint32_t)lonBuf[CAN_FRAME_GPS_EXT_LON_B3] << 24)
                          | ((uint32_t)lonBuf[CAN_FRAME_GPS_EXT_LON_B2] << 16)
                          | ((uint32_t)lonBuf[CAN_FRAME_GPS_EXT_LON_B1] << 8)
                          | (uint32_t)lonBuf[CAN_FRAME_GPS_EXT_LON_B0]);

    TEST_ASSERT_EQUAL_INT(52229700, lat); // 52.229700 * 1e6
    TEST_ASSERT_EQUAL_INT(21012200, lon); // 21.012200 * 1e6

    uint32_t packedDt = ((uint32_t)lonBuf[CAN_FRAME_GPS_EXT_DT_HI] << 16)
                      | ((uint32_t)lonBuf[CAN_FRAME_GPS_EXT_DT_MD] << 8)
                      | (uint32_t)lonBuf[CAN_FRAME_GPS_EXT_DT_LO];
    TEST_ASSERT_EQUAL_UINT32(CAN_packGpsDateTime(260403, 1427), packedDt);
}

void test_build_gps_lat_lon_frames_clamp_coordinates(void) {
    setGlobalValue(F_LATITUDE, 120.0f);
    setGlobalValue(F_LONGITUDE, -250.0f);
    setGlobalValue(F_GPS_IS_AVAILABLE, 1.0f);
    setGlobalValue(F_GPS_DATE, 260403.0f);
    setGlobalValue(F_GPS_TIME, 1427.0f);

    uint8_t latBuf[CAN_FRAME_MAX_LENGTH] = {0};
    uint8_t lonBuf[CAN_FRAME_MAX_LENGTH] = {0};
    TEST_ASSERT_TRUE(CAN_buildGpsLatFrame(0x01, latBuf, (int)sizeof(latBuf)));
    TEST_ASSERT_TRUE(CAN_buildGpsLonTimeFrame(0x02, lonBuf, (int)sizeof(lonBuf)));

    int32_t lat = (int32_t)(((uint32_t)latBuf[CAN_FRAME_GPS_EXT_LAT_B3] << 24)
                          | ((uint32_t)latBuf[CAN_FRAME_GPS_EXT_LAT_B2] << 16)
                          | ((uint32_t)latBuf[CAN_FRAME_GPS_EXT_LAT_B1] << 8)
                          | (uint32_t)latBuf[CAN_FRAME_GPS_EXT_LAT_B0]);
    int32_t lon = (int32_t)(((uint32_t)lonBuf[CAN_FRAME_GPS_EXT_LON_B3] << 24)
                          | ((uint32_t)lonBuf[CAN_FRAME_GPS_EXT_LON_B2] << 16)
                          | ((uint32_t)lonBuf[CAN_FRAME_GPS_EXT_LON_B1] << 8)
                          | (uint32_t)lonBuf[CAN_FRAME_GPS_EXT_LON_B0]);

    TEST_ASSERT_EQUAL_INT(90000000, lat);
    TEST_ASSERT_EQUAL_INT(-180000000, lon);
}

void test_truncated_egt_frame_is_ignored(void) {
    setGlobalValue(F_EGT, 321.0f);
    setGlobalValue(F_DPF_TEMP, 654.0f);

    uint8_t shortFrame[3] = {0x00, 0x12, 0x34};
    hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_EGT_UPDATE, 3, shortFrame);
    canMainLoop();

    TEST_ASSERT_EQUAL_FLOAT(321.0f, getGlobalValue(F_EGT));
    TEST_ASSERT_EQUAL_FLOAT(654.0f, getGlobalValue(F_DPF_TEMP));
}

void test_truncated_oil_and_speed_frame_is_ignored(void) {
    setGlobalValue(F_OIL_PRESSURE, 4.2f);
    setGlobalValue(F_ABS_CAR_SPEED, 88.0f);

    uint8_t shortFrame[2] = {0x01, 0x02};
    hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_OIL_AND_SPEED_MODULE_UPDATE, 2, shortFrame);
    canMainLoop();

    TEST_ASSERT_EQUAL_FLOAT(4.2f, getGlobalValue(F_OIL_PRESSURE));
    TEST_ASSERT_EQUAL_FLOAT(88.0f, getGlobalValue(F_ABS_CAR_SPEED));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pack_gps_datetime_valid);
    RUN_TEST(test_pack_gps_datetime_invalid_returns_zero);
    RUN_TEST(test_build_gps_lat_lon_frames_encode_high_precision_values);
    RUN_TEST(test_build_gps_lat_lon_frames_clamp_coordinates);
    RUN_TEST(test_truncated_egt_frame_is_ignored);
    RUN_TEST(test_truncated_oil_and_speed_frame_is_ignored);
    return UNITY_END();
}
