#include "unity.h"
#include "obd-2.h"
#include "obd-2_mapping.h"
#include "sensors.h"
#include "gps.h"
#include "testable/obd2_testable.h"
#include "hal/hal_eeprom.h"
#include "hal/impl/.mock/hal_mock.h"

/*
 * OBD-2 unit tests - pure function tests for Mode 01 PID encoding,
 * Ford part number helpers, DTC payload builder, and TOTDIST getter/setter.
 *
 * These tests do NOT require CAN bus setup - they exercise the encoding
 * logic directly via encodeMode01PidData() and helper functions.
 */

static void ensure_obd_can_ready(void) {
    if (obdTestGetCanHandle() == NULL) {
        obdInit(1);
    }
    TEST_ASSERT_NOT_NULL(obdTestGetCanHandle());
    hal_mock_can_reset(obdTestGetCanHandle());
}

static bool pop_can_tx(uint32_t *id, uint8_t *len, uint8_t *data) {
    return hal_mock_can_get_sent(obdTestGetCanHandle(), id, len, data);
}

void setUp(void) {
    ensure_obd_can_ready();
    dtcManagerClearAll();
    for (int i = 0; i < F_LAST; i++) {
        setGlobalValue(i, 0.0f);
    }
}

void tearDown(void) {}

// ── Mode 01 PID encoding ─────────────────────────────────────────────────────

void test_pid_coolant_temp_at_zero(void) {
    // PID 0x05: A = temp + 40 -> 0 + 40 = 40
    setGlobalValue(F_COOLANT_TEMP, 0.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(ENGINE_COOLANT_TEMP, data, &len));
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_UINT8(40, data[0]);
}

void test_pid_coolant_temp_at_80(void) {
    // PID 0x05: A = 80 + 40 = 120
    setGlobalValue(F_COOLANT_TEMP, 80.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(ENGINE_COOLANT_TEMP, data, &len));
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_UINT8(120, data[0]);
}

void test_pid_rpm_at_idle(void) {
    // PID 0x0C: RPM × 4, 2 bytes big-endian. 850 RPM -> 3400 = 0x0D48
    setGlobalValue(F_RPM, 850.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(ENGINE_RPM, data, &len));
    TEST_ASSERT_EQUAL_INT(2, len);
    uint16_t result = ((uint16_t)data[0] << 8) | data[1];
    TEST_ASSERT_EQUAL_UINT16(3400, result);
}

void test_pid_rpm_at_zero(void) {
    setGlobalValue(F_RPM, 0.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(ENGINE_RPM, data, &len));
    TEST_ASSERT_EQUAL_INT(2, len);
    TEST_ASSERT_EQUAL_UINT8(0, data[0]);
    TEST_ASSERT_EQUAL_UINT8(0, data[1]);
}

void test_pid_vehicle_speed(void) {
    // PID 0x0D: 1 byte, km/h
    setGlobalValue(F_ABS_CAR_SPEED, 100.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(VEHICLE_SPEED, data, &len));
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_UINT8(100, data[0]);
}

void test_pid_intake_temp(void) {
    // PID 0x0F: A = temp + 40. At 25°C -> 65
    setGlobalValue(F_INTAKE_TEMP, 25.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(INTAKE_TEMP, data, &len));
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_UINT8(65, data[0]);
}

void test_pid_ecu_voltage(void) {
    // PID 0x42: V × 1000, 2 bytes. 13.8V -> 13800 = 0x35E8
    setGlobalValue(F_VOLTS, 13.8f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(ECU_VOLTAGE, data, &len));
    TEST_ASSERT_EQUAL_INT(2, len);
    uint16_t result = ((uint16_t)data[0] << 8) | data[1];
    TEST_ASSERT_EQUAL_UINT16(13800, result);
}

void test_pid_oil_temp(void) {
    // PID 0x5C: A = temp + 40. At 90°C -> 130
    setGlobalValue(F_OIL_TEMP, 90.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(ENGINE_OIL_TEMP, data, &len));
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_UINT8(130, data[0]);
}

void test_pid_fuel_type_diesel(void) {
    // PID 0x51: should return FUEL_TYPE_DIESEL = 4
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(FUEL_TYPE, data, &len));
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_UINT8(FUEL_TYPE_DIESEL, data[0]);
}

void test_pid_dpf_temp_from_sensor(void) {
    // PID 0x7C: (A×256+B)/10 - 40 = temp -> raw = (temp+40)×10
    // At 300°C -> raw = 3400 = 0x0D48
    setGlobalValue(F_DPF_TEMP, 300.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(P_DPF_TEMP, data, &len));
    TEST_ASSERT_EQUAL_INT(2, len);
    uint16_t result = ((uint16_t)data[0] << 8) | data[1];
    TEST_ASSERT_EQUAL_UINT16(3400, result);
}

void test_pid_dpf_temp_at_zero(void) {
    // 0°C -> raw = (0+40)×10 = 400
    setGlobalValue(F_DPF_TEMP, 0.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(P_DPF_TEMP, data, &len));
    TEST_ASSERT_EQUAL_INT(2, len);
    uint16_t result = ((uint16_t)data[0] << 8) | data[1];
    TEST_ASSERT_EQUAL_UINT16(400, result);
}

void test_pid_engine_load(void) {
    // PID 0x04: A = load% × 255/100. At 50% -> 127 (floored)
    setGlobalValue(F_CALCULATED_ENGINE_LOAD, 50.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(ENGINE_LOAD, data, &len));
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_INT_WITHIN(2, 127, data[0]);
}

void test_pid_intake_pressure_uses_absolute_kpa(void) {
    setGlobalValue(F_PRESSURE, 0.80f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(INTAKE_PRESSURE, data, &len));
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_UINT8(181, data[0]);
}

void test_pid_intake_pressure_clamps_to_255_kpa(void) {
    setGlobalValue(F_PRESSURE, 3.50f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(INTAKE_PRESSURE, data, &len));
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_UINT8(255, data[0]);
}

void test_pid_unsupported_returns_false(void) {
    // PID 0xFE is not in the handler table.
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_FALSE(encodeMode01PidData(0xFE, data, &len));
}

void test_pid_egt_as_catalyst_temp(void) {
    // PID 0x3C (CAT_TEMP_B1S1): (A×256+B)/10 - 40 = temp
    // At 600°C -> raw = (600+40)×10 = 6400
    setGlobalValue(F_EGT, 600.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(CAT_TEMP_B1S1, data, &len));
    TEST_ASSERT_EQUAL_INT(2, len);
    uint16_t result = ((uint16_t)data[0] << 8) | data[1];
    TEST_ASSERT_EQUAL_UINT16(6400, result);
}

void test_stmin_to_ms_preserves_millisecond_values(void) {
    TEST_ASSERT_EQUAL_UINT8(0x00, stMinToMs(0x00));
    TEST_ASSERT_EQUAL_UINT8(0x7F, stMinToMs(0x7F));
}

void test_stmin_to_ms_clamps_submillisecond_range_to_one_ms(void) {
    TEST_ASSERT_EQUAL_UINT8(1, stMinToMs(0xF1));
    TEST_ASSERT_EQUAL_UINT8(1, stMinToMs(0xF9));
}

void test_stmin_to_ms_rejects_reserved_values(void) {
    TEST_ASSERT_EQUAL_UINT8(0, stMinToMs(0x80));
    TEST_ASSERT_EQUAL_UINT8(0, stMinToMs(0xFA));
}

// ── Ford part number split ────────────────────────────────────────────────────

void test_ford_part_split_standard(void) {
    const char *prefix, *middle, *suffix;
    int pLen, mLen, sLen;
    TEST_ASSERT_TRUE(fordPartNumberSplit("YS6F-12A650-FA",
                                         &prefix, &pLen, &middle, &mLen, &suffix, &sLen));
    TEST_ASSERT_EQUAL_INT(4, pLen);
    TEST_ASSERT_EQUAL_INT(6, mLen);
    TEST_ASSERT_EQUAL_INT(2, sLen);
    TEST_ASSERT_EQUAL_INT(0, memcmp(prefix, "YS6F", 4));
    TEST_ASSERT_EQUAL_INT(0, memcmp(middle, "12A650", 6));
    TEST_ASSERT_EQUAL_INT(0, memcmp(suffix, "FA", 2));
}

void test_ford_part_split_three_char_suffix(void) {
    const char *prefix, *middle, *suffix;
    int pLen, mLen, sLen;
    TEST_ASSERT_TRUE(fordPartNumberSplit("XS4A-12A650-AXB",
                                         &prefix, &pLen, &middle, &mLen, &suffix, &sLen));
    TEST_ASSERT_EQUAL_INT(4, pLen);
    TEST_ASSERT_EQUAL_INT(6, mLen);
    TEST_ASSERT_EQUAL_INT(3, sLen);
    TEST_ASSERT_EQUAL_INT(0, memcmp(suffix, "AXB", 3));
}

void test_ford_part_split_no_dashes_fails(void) {
    const char *prefix, *middle, *suffix;
    int pLen, mLen, sLen;
    TEST_ASSERT_FALSE(fordPartNumberSplit("NODASHES",
                                          &prefix, &pLen, &middle, &mLen, &suffix, &sLen));
}

void test_ford_part_split_one_dash_fails(void) {
    const char *prefix, *middle, *suffix;
    int pLen, mLen, sLen;
    TEST_ASSERT_FALSE(fordPartNumberSplit("PREFIX-REST",
                                          &prefix, &pLen, &middle, &mLen, &suffix, &sLen));
}

// ── Ford part number suffix encoding ──────────────────────────────────────────

void test_ford_suffix_single_char_A(void) {
    // A is index 0 in charset ABCDEFGHJKLMNPRSTUVXYZ
    TEST_ASSERT_EQUAL_UINT8(0, fordPartSuffixCharsToByte("A", 1));
}

void test_ford_suffix_single_char_B(void) {
    TEST_ASSERT_EQUAL_UINT8(1, fordPartSuffixCharsToByte("B", 1));
}

void test_ford_suffix_two_chars_AX(void) {
    // A=0, X=19 in charset. Two chars: (0+1)*22 + 19 = 41
    TEST_ASSERT_EQUAL_UINT8(41, fordPartSuffixCharsToByte("AX", 2));
}

void test_ford_suffix_two_chars_FA(void) {
    // F=5, A=0. Two chars: (5+1)*22 + 0 = 132
    TEST_ASSERT_EQUAL_UINT8(132, fordPartSuffixCharsToByte("FA", 2));
}

// ── fillDtcPayload ────────────────────────────────────────────────────────────

void test_fill_dtc_payload_empty(void) {
    // No DTCs should produce: [responseService, 0x00]
    dtcManagerClearAll();
    uint8_t buf[24] = {0};
    int len = fillDtcPayload(0x43, DTC_KIND_STORED, buf, (int)sizeof(buf));
    TEST_ASSERT_EQUAL_INT(2, len);
    TEST_ASSERT_EQUAL_UINT8(0x43, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[1]);
}

void test_fill_dtc_payload_with_codes(void) {
    dtcManagerClearAll();
    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, true);
    dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, true);
    uint8_t buf[24] = {0};
    int len = fillDtcPayload(0x43, DTC_KIND_ACTIVE, buf, (int)sizeof(buf));
    // Header(2) + 2 codes × 2 bytes = 6
    TEST_ASSERT_GREATER_OR_EQUAL(6, len);
    TEST_ASSERT_EQUAL_UINT8(0x43, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(2, buf[1]);
}

void test_fill_dtc_payload_null_safety(void) {
    TEST_ASSERT_EQUAL_INT(0, fillDtcPayload(0x43, DTC_KIND_STORED, NULL, 24));
}

void test_fill_dtc_payload_small_buffer(void) {
    uint8_t buf[1] = {0};
    TEST_ASSERT_EQUAL_INT(0, fillDtcPayload(0x43, DTC_KIND_STORED, buf, 1));
}

void test_dtc_timestamp_first_occurrence_from_gps(void) {
    dtcManagerClearAll();
    hal_mock_gps_reset();
    hal_mock_gps_set_valid(true);
    hal_mock_gps_set_age(0);
    hal_mock_gps_set_date(2026, 1, 1);
    hal_mock_gps_set_time(12, 0, 0);

    uint32_t expected = gpsGetEpoch();
    TEST_ASSERT_GREATER_THAN_UINT32(0u, expected);

    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, true);
    TEST_ASSERT_EQUAL_UINT32(expected, dtcManagerGetTimestamp(DTC_OBD_CAN_INIT_FAIL));
}

void test_dtc_timestamp_not_overwritten_on_reactivation(void) {
    dtcManagerClearAll();
    hal_mock_gps_reset();
    hal_mock_gps_set_valid(true);
    hal_mock_gps_set_age(0);
    hal_mock_gps_set_date(2026, 1, 1);
    hal_mock_gps_set_time(12, 0, 0);

    dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, true);
    uint32_t firstTs = dtcManagerGetTimestamp(DTC_PCF8574_COMM_FAIL);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, firstTs);

    dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, false);
    hal_mock_gps_set_time(13, 0, 0);
    dtcManagerSetActive(DTC_PCF8574_COMM_FAIL, true);

    TEST_ASSERT_EQUAL_UINT32(firstTs, dtcManagerGetTimestamp(DTC_PCF8574_COMM_FAIL));
}

// ── TOTDIST getter/setter ─────────────────────────────────────────────────────

#ifdef OBD_ENABLE_TOTDIST
void test_totdist_default_value(void) {
    // After init, should return ecu_TotalDistanceKmDefault
    obdSetTotalDistanceKm(ecu_TotalDistanceKmDefault);
    TEST_ASSERT_EQUAL_UINT32(ecu_TotalDistanceKmDefault, obdGetTotalDistanceKm());
}

void test_totdist_set_and_get(void) {
    obdSetTotalDistanceKm(123456);
    TEST_ASSERT_EQUAL_UINT32(123456, obdGetTotalDistanceKm());
}

void test_totdist_set_zero(void) {
    obdSetTotalDistanceKm(0);
    TEST_ASSERT_EQUAL_UINT32(0, obdGetTotalDistanceKm());
}

void test_totdist_set_max_3byte(void) {
    // 3-byte max = 0xFFFFFF = 16777215
    obdSetTotalDistanceKm(16777215);
    TEST_ASSERT_EQUAL_UINT32(16777215, obdGetTotalDistanceKm());
}
#endif

// ── obd_encodeTempByte helper ────────────────────────────────────────────────

void test_obd_temp_byte_zero_celsius(void) {
    TEST_ASSERT_EQUAL_UINT8(40, obd_encodeTempByte(0.0f));
}

void test_obd_temp_byte_negative_40_celsius(void) {
    TEST_ASSERT_EQUAL_UINT8(0, obd_encodeTempByte(-40.0f));
}

void test_obd_temp_byte_215_celsius_is_max(void) {
    TEST_ASSERT_EQUAL_UINT8(255, obd_encodeTempByte(215.0f));
}

void test_obd_temp_byte_clamps_below_minus_40(void) {
    TEST_ASSERT_EQUAL_UINT8(0, obd_encodeTempByte(-100.0f));
}

void test_obd_temp_byte_clamps_above_215(void) {
    TEST_ASSERT_EQUAL_UINT8(255, obd_encodeTempByte(500.0f));
}

void test_obd_temp_byte_positive_mid(void) {
    // 90 °C -> raw = 130
    TEST_ASSERT_EQUAL_UINT8(130, obd_encodeTempByte(90.0f));
}

void test_obd_temp_byte_fractional_truncates_toward_zero(void) {
    // 24.9 + 40 = 64.9 -> truncated to 64
    TEST_ASSERT_EQUAL_UINT8(64, obd_encodeTempByte(24.9f));
}

void test_obd_temp_byte_applied_via_coolant_pid(void) {
    // Sanity check: Mode 01 PID 0x05 uses the helper. -50 °C must clamp to 0.
    setGlobalValue(F_COOLANT_TEMP, -50.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(ENGINE_COOLANT_TEMP, data, &len));
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_UINT8(0, data[0]);
}

void test_obd_temp_byte_applied_via_intake_pid(void) {
    // 250 °C beyond spec must clamp to 255 via the helper.
    setGlobalValue(F_INTAKE_TEMP, 250.0f);
    uint8_t data[4] = {0};
    int len = 0;
    TEST_ASSERT_TRUE(encodeMode01PidData(INTAKE_TEMP, data, &len));
    TEST_ASSERT_EQUAL_INT(1, len);
    TEST_ASSERT_EQUAL_UINT8(255, data[0]);
}

// ── UDS/KWP request handling via obdReq ──────────────────────────────────────

void test_uds_diag_session_extended_positive_response(void) {
    uint8_t req[8] = {0x02, UDS_SVC_DIAGNOSTIC_SESSION, UDS_SESSION_EXTENDED, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(8, len);
    TEST_ASSERT_EQUAL_UINT8(0x06, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_DIAGNOSTIC_SESSION, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SESSION_EXTENDED, tx[2]);
}

void test_obd_request_with_short_dlc_is_ignored(void) {
    uint8_t req[1] = {0x01};
    obdReqWithDlc(LISTEN_ID, 1u, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_FALSE(pop_can_tx(&id, &len, tx));
}

void test_obd_request_with_declared_length_beyond_dlc_is_ignored(void) {
    uint8_t req[3] = {0x03, UDS_SVC_READ_DATA_BY_ID, 0xF1};
    obdReqWithDlc(LISTEN_ID, 3u, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_FALSE(pop_can_tx(&id, &len, tx));
}

void test_uds_diag_session_unsupported_subfunction_negative_response(void) {
    uint8_t req[8] = {0x02, UDS_SVC_DIAGNOSTIC_SESSION, 0x7Eu, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(8, len);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_DIAGNOSTIC_SESSION, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_SUBFUNCTION_NOT_SUPPORTED, tx[3]);
}

void test_uds_tester_present_suppress_positive_response_sends_nothing(void) {
    uint8_t req[8] = {0x02, UDS_SVC_TESTER_PRESENT, UDS_SUPPRESS_POSITIVE_RSP, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_FALSE(pop_can_tx(&id, &len, tx));
}

void test_uds_tester_present_without_suppress_returns_positive(void) {
    uint8_t req[8] = {0x02, UDS_SVC_TESTER_PRESENT, 0x01, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x02, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_TESTER_PRESENT, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, tx[2]);
}

void test_uds_read_active_session_did_reflects_last_session(void) {
    uint8_t sessionReq[8] = {0x02, UDS_SVC_DIAGNOSTIC_SESSION, UDS_SESSION_EXTENDED, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, sessionReq);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx)); // consume 0x10 response

    uint8_t didReq[8] = {0x03, UDS_SVC_READ_DATA_BY_ID, MSB(DID_ACTIVE_SESSION), LSB(DID_ACTIVE_SESSION), 0, 0, 0, 0};
    obdReq(LISTEN_ID, didReq);
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x04, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_READ_DATA_BY_ID, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(MSB(DID_ACTIVE_SESSION), tx[2]);
    TEST_ASSERT_EQUAL_UINT8(LSB(DID_ACTIVE_SESSION), tx[3]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SESSION_EXTENDED, tx[4]);
}

void test_kwp_read_data_by_local_id_unknown_returns_nrc31(void) {
    uint8_t req[8] = {0x02, UDS_SVC_READ_DATA_BY_LOCAL_ID, 0x55, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_READ_DATA_BY_LOCAL_ID, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_REQUEST_OUT_OF_RANGE, tx[3]);
}

void test_uds_comm_control_invalid_subfunction_returns_nrc22(void) {
    uint8_t req[8] = {0x02, UDS_SVC_COMM_CONTROL, 0x03, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_COMM_CONTROL, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_CONDITIONS_NOT_CORRECT, tx[3]);
}

void test_uds_control_dtc_setting_on_positive_response(void) {
    uint8_t req[8] = {0x02, UDS_SVC_CONTROL_DTC_SETTING, 0x01, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x02, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_CONTROL_DTC_SETTING, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, tx[2]);
}

void test_uds_control_dtc_setting_invalid_subfunction_returns_nrc12(void) {
    uint8_t req[8] = {0x02, UDS_SVC_CONTROL_DTC_SETTING, 0x03, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_CONTROL_DTC_SETTING, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_SUBFUNCTION_NOT_SUPPORTED, tx[3]);
}

void test_uds_ecu_reset_echoes_subfunction(void) {
    uint8_t req[8] = {0x02, UDS_SVC_ECU_RESET, 0x03, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x02, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_ECU_RESET, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[2]);
}

void test_uds_security_access_returns_nrc22(void) {
    uint8_t req[8] = {0x02, UDS_SVC_SECURITY_ACCESS, 0x01, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_SECURITY_ACCESS, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_CONDITIONS_NOT_CORRECT, tx[3]);
}

void test_uds_routine_control_returns_nrc31(void) {
    uint8_t req[8] = {0x02, UDS_SVC_ROUTINE_CONTROL, 0x01, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_ROUTINE_CONTROL, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_REQUEST_OUT_OF_RANGE, tx[3]);
}

void test_uds_read_dtc_info_returns_service_not_supported_in_fordiag_mode(void) {
    uint8_t req[8] = {0x01, UDS_SVC_READ_DTC_INFO, 0, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_READ_DTC_INFO, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_SERVICE_NOT_SUPPORTED, tx[3]);
}

void test_kwp_read_dtc_by_status_reports_stored_code(void) {
    uint16_t codes[1] = {0};
    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, true);
    TEST_ASSERT_EQUAL_UINT8(1, dtcManagerGetCodes(DTC_KIND_STORED, codes, 1));

    uint8_t req[8] = {0x04, KWP_SVC_READ_DTC_BY_STATUS, 0x00, 0xFF, 0xFF, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x05, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(KWP_RSP_READ_DTC_BY_STATUS, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(MSB(codes[0]), tx[3]);
    TEST_ASSERT_EQUAL_UINT8(LSB(codes[0]), tx[4]);
    TEST_ASSERT_EQUAL_UINT8(0x08, tx[5]);
}

void test_kwp_read_dtc_by_status_reports_active_code(void) {
    uint16_t codes[1] = {0};
    dtcManagerSetActive(DTC_OBD_CAN_INIT_FAIL, true);
    TEST_ASSERT_EQUAL_UINT8(1, dtcManagerGetCodes(DTC_KIND_ACTIVE, codes, 1));

    uint8_t req[8] = {0x04, KWP_SVC_READ_DTC_BY_STATUS, 0x01, 0xFF, 0xFF, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x05, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(KWP_RSP_READ_DTC_BY_STATUS, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(MSB(codes[0]), tx[3]);
    TEST_ASSERT_EQUAL_UINT8(LSB(codes[0]), tx[4]);
    TEST_ASSERT_EQUAL_UINT8(0x01, tx[5]);
}

void test_uds_read_memory_by_addr_unmapped_returns_zero_filled_block(void) {
    uint8_t req[8] = {0x04, UDS_SVC_READ_MEMORY_BY_ADDR, 0x00, 0x12, 0x34, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x07, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_READ_MEMORY_BY_ADDR, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(0x12, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(0x34, tx[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00, tx[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00, tx[5]);
    TEST_ASSERT_EQUAL_UINT8(0x00, tx[6]);
    TEST_ASSERT_EQUAL_UINT8(0x00, tx[7]);
}

void test_uds_read_memory_by_addr_invalid_type_returns_general_negative(void) {
    uint8_t req[8] = {0x04, UDS_SVC_READ_MEMORY_BY_ADDR, 0x7F, 0x12, 0x34, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x06, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_READ_MEMORY_BY_ADDR, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(0x7F, tx[3]);
    TEST_ASSERT_EQUAL_UINT8(0x12, tx[4]);
    TEST_ASSERT_EQUAL_UINT8(0x34, tx[5]);
    TEST_ASSERT_EQUAL_UINT8(NRC_SUBFUNCTION_NOT_SUPPORTED, tx[6]);
}

void test_uds_read_ford_boost_did_returns_pressure_payload(void) {
    setGlobalValue(F_PRESSURE, 1.25f);
    uint8_t req[8] = {0x03, UDS_SVC_READ_DATA_BY_ID, MSB(DID_FORD_BOOST), LSB(DID_FORD_BOOST), 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x05, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_READ_DATA_BY_ID, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(MSB(DID_FORD_BOOST), tx[2]);
    TEST_ASSERT_EQUAL_UINT8(LSB(DID_FORD_BOOST), tx[3]);
    TEST_ASSERT_EQUAL_UINT8(0x04, tx[4]);
    TEST_ASSERT_EQUAL_UINT8(0xE2, tx[5]);
}

void test_uds_read_ecu_capabilities_returns_nrc31(void) {
    uint8_t req[8] = {0x03, UDS_SVC_READ_DATA_BY_ID, MSB(DID_ECU_CAPABILITIES), LSB(DID_ECU_CAPABILITIES), 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_READ_DATA_BY_ID, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_REQUEST_OUT_OF_RANGE, tx[3]);
}

void test_uds_read_unknown_did_returns_nrc31(void) {
    uint8_t req[8] = {0x03, UDS_SVC_READ_DATA_BY_ID, 0xFE, 0xEE, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_READ_DATA_BY_ID, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_REQUEST_OUT_OF_RANGE, tx[3]);
}

void test_uds_read_scp_load_returns_scaled_word(void) {
    setGlobalValue(F_CALCULATED_ENGINE_LOAD, 50.0f);
    uint8_t req[8] = {0x03, UDS_SVC_READ_DATA_BY_ID, MSB(SCP_PID_LOAD), LSB(SCP_PID_LOAD), 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x05, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_READ_DATA_BY_ID, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(MSB(SCP_PID_LOAD), tx[2]);
    TEST_ASSERT_EQUAL_UINT8(LSB(SCP_PID_LOAD), tx[3]);
    TEST_ASSERT_EQUAL_UINT8(0x40, tx[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00, tx[5]);
}

void test_uds_read_scp_bp_returns_default_barometric_value(void) {
    uint8_t req[8] = {0x03, UDS_SVC_READ_DATA_BY_ID, MSB(SCP_PID_BP), LSB(SCP_PID_BP), 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x04, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_READ_DATA_BY_ID, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(MSB(SCP_PID_BP), tx[2]);
    TEST_ASSERT_EQUAL_UINT8(LSB(SCP_PID_BP), tx[3]);
    TEST_ASSERT_EQUAL_UINT8(239, tx[4]);
}

void test_uds_read_scp_idblock_addr_returns_descriptor(void) {
    uint8_t req[8] = {0x03, UDS_SVC_READ_DATA_BY_ID, MSB(SCP_PID_IDBLOCK_ADDR), LSB(SCP_PID_IDBLOCK_ADDR), 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x07, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_READ_DATA_BY_ID, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(MSB(SCP_PID_IDBLOCK_ADDR), tx[2]);
    TEST_ASSERT_EQUAL_UINT8(LSB(SCP_PID_IDBLOCK_ADDR), tx[3]);
    TEST_ASSERT_EQUAL_UINT8(SCP_IDBLOCK_BANK, tx[4]);
    TEST_ASSERT_EQUAL_UINT8(MSB(SCP_IDBLOCK_ADDR), tx[5]);
    TEST_ASSERT_EQUAL_UINT8(LSB(SCP_IDBLOCK_ADDR), tx[6]);
    TEST_ASSERT_EQUAL_UINT8(SCP_IDBLOCK_FMT_DEFAULT, tx[7]);
}

void test_uds_read_f4_catch_code_alt_prefers_live_pid_payload(void) {
    setGlobalValue(F_INTAKE_TEMP, 25.0f);
    uint8_t req[8] = {0x03, UDS_SVC_READ_DATA_BY_ID, MSB(DID_F4_CATCH_CODE_ALT), LSB(DID_F4_CATCH_CODE_ALT), 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x04, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_READ_DATA_BY_ID, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(MSB(DID_F4_CATCH_CODE_ALT), tx[2]);
    TEST_ASSERT_EQUAL_UINT8(LSB(DID_F4_CATCH_CODE_ALT), tx[3]);
    TEST_ASSERT_EQUAL_UINT8(65, tx[4]);
}

void test_uds_read_scp_maf_pid_returns_nrc31(void) {
    uint16_t pids[] = {SCP_PID_IMAF, SCP_PID_VMAF, SCP_PID_MAF_RATE};

    for (size_t index = 0; index < (sizeof(pids) / sizeof(pids[0])); index++) {
        uint8_t req[8] = {0x03, UDS_SVC_READ_DATA_BY_ID, MSB(pids[index]), LSB(pids[index]), 0, 0, 0, 0};
        obdReq(LISTEN_ID, req);

        uint32_t id = 0;
        uint8_t len = 0;
        uint8_t tx[8] = {0};
        TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
        TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
        TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
        TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
        TEST_ASSERT_EQUAL_UINT8(UDS_SVC_READ_DATA_BY_ID, tx[2]);
        TEST_ASSERT_EQUAL_UINT8(NRC_REQUEST_OUT_OF_RANGE, tx[3]);
    }
}

void test_kwp_read_local_id_rom_size_returns_512k(void) {
    uint8_t req[8] = {0x02, UDS_SVC_READ_DATA_BY_LOCAL_ID, KWP_LID_ROM_SIZE, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x06, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_READ_DATA_BY_LOCAL_ID, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(KWP_LID_ROM_SIZE, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, tx[3]);
    TEST_ASSERT_EQUAL_UINT8(0x08, tx[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00, tx[5]);
    TEST_ASSERT_EQUAL_UINT8(0x00, tx[6]);
}

void test_kwp_read_local_id_compact_ident_returns_nrc31(void) {
    uint8_t req[8] = {0x02, UDS_SVC_READ_DATA_BY_LOCAL_ID, KWP_LID_COMPACT_IDENT, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_READ_DATA_BY_LOCAL_ID, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_REQUEST_OUT_OF_RANGE, tx[3]);
}

// ── UDS short-frame negative-response regression guards (NRC 0x13) ───────────
// These cover the requireMinLength() boolean contract at call sites where
// commit 04cebf9 refactored the early-return style into single-exit; a future
// accidental polarity flip or missing requireMinLength() call will surface
// here instead of silently letting an under-sized UDS request fall through.

void test_uds_diag_session_short_frame_returns_nrc13(void) {
    // UDS_SVC_DIAGNOSTIC_SESSION requires minLen=2 (service + subFunction).
    // numofBytes=1 omits the subFunction and must trigger NRC_INCORRECT_LENGTH.
    uint8_t req[8] = {0x01, UDS_SVC_DIAGNOSTIC_SESSION, 0, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_DIAGNOSTIC_SESSION, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_INCORRECT_LENGTH, tx[3]);
}

void test_uds_read_data_by_id_short_frame_returns_nrc13(void) {
    // UDS_SVC_READ_DATA_BY_ID requires minLen=3 (service + 2-byte DID).
    // numofBytes=2 supplies only one DID byte and must trigger NRC_INCORRECT_LENGTH.
    uint8_t req[8] = {0x02, UDS_SVC_READ_DATA_BY_ID, 0xF1, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_READ_DATA_BY_ID, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_INCORRECT_LENGTH, tx[3]);
}

void test_uds_read_memory_by_addr_short_frame_returns_nrc13(void) {
    // UDS_SVC_READ_MEMORY_BY_ADDR requires minLen=4 (service + DMR type + 2-byte addr).
    // numofBytes=3 is one byte short and must trigger NRC_INCORRECT_LENGTH.
    uint8_t req[8] = {0x03, UDS_SVC_READ_MEMORY_BY_ADDR, 0x00, 0x10, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_READ_MEMORY_BY_ADDR, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_INCORRECT_LENGTH, tx[3]);
}

void test_uds_control_dtc_setting_short_frame_returns_nrc13(void) {
    // UDS_SVC_CONTROL_DTC_SETTING (last requireMinLength() site in the
    // dispatch chain) requires minLen=2. numofBytes=1 must trigger
    // NRC_INCORRECT_LENGTH rather than falling through to a positive ack.
    uint8_t req[8] = {0x01, UDS_SVC_CONTROL_DTC_SETTING, 0, 0, 0, 0, 0, 0};
    obdReq(LISTEN_ID, req);

    uint32_t id = 0;
    uint8_t len = 0;
    uint8_t tx[8] = {0};
    TEST_ASSERT_TRUE(pop_can_tx(&id, &len, tx));
    TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
    TEST_ASSERT_EQUAL_UINT8(0x03, tx[0]);
    TEST_ASSERT_EQUAL_UINT8(UDS_RSP_NEGATIVE, tx[1]);
    TEST_ASSERT_EQUAL_UINT8(UDS_SVC_CONTROL_DTC_SETTING, tx[2]);
    TEST_ASSERT_EQUAL_UINT8(NRC_INCORRECT_LENGTH, tx[3]);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(void) {
    hal_eeprom_init(HAL_EEPROM_RP2040, 512, 0);
    initSensors();

    UNITY_BEGIN();

    // Mode 01 PID encoding
    RUN_TEST(test_pid_coolant_temp_at_zero);
    RUN_TEST(test_pid_coolant_temp_at_80);
    RUN_TEST(test_pid_rpm_at_idle);
    RUN_TEST(test_pid_rpm_at_zero);
    RUN_TEST(test_pid_vehicle_speed);
    RUN_TEST(test_pid_intake_temp);
    RUN_TEST(test_pid_ecu_voltage);
    RUN_TEST(test_pid_oil_temp);
    RUN_TEST(test_pid_fuel_type_diesel);
    RUN_TEST(test_pid_dpf_temp_from_sensor);
    RUN_TEST(test_pid_dpf_temp_at_zero);
    RUN_TEST(test_pid_engine_load);
    RUN_TEST(test_pid_intake_pressure_uses_absolute_kpa);
    RUN_TEST(test_pid_intake_pressure_clamps_to_255_kpa);
    RUN_TEST(test_pid_unsupported_returns_false);
    RUN_TEST(test_pid_egt_as_catalyst_temp);

    // obd_encodeTempByte helper
    RUN_TEST(test_obd_temp_byte_zero_celsius);
    RUN_TEST(test_obd_temp_byte_negative_40_celsius);
    RUN_TEST(test_obd_temp_byte_215_celsius_is_max);
    RUN_TEST(test_obd_temp_byte_clamps_below_minus_40);
    RUN_TEST(test_obd_temp_byte_clamps_above_215);
    RUN_TEST(test_obd_temp_byte_positive_mid);
    RUN_TEST(test_obd_temp_byte_fractional_truncates_toward_zero);
    RUN_TEST(test_obd_temp_byte_applied_via_coolant_pid);
    RUN_TEST(test_obd_temp_byte_applied_via_intake_pid);
    RUN_TEST(test_obd_request_with_short_dlc_is_ignored);
    RUN_TEST(test_obd_request_with_declared_length_beyond_dlc_is_ignored);

    // UDS/KWP request handling
    RUN_TEST(test_uds_diag_session_extended_positive_response);
    RUN_TEST(test_uds_diag_session_unsupported_subfunction_negative_response);
    RUN_TEST(test_uds_tester_present_suppress_positive_response_sends_nothing);
    RUN_TEST(test_uds_tester_present_without_suppress_returns_positive);
    RUN_TEST(test_uds_read_active_session_did_reflects_last_session);
    RUN_TEST(test_kwp_read_data_by_local_id_unknown_returns_nrc31);
    RUN_TEST(test_uds_comm_control_invalid_subfunction_returns_nrc22);
    RUN_TEST(test_uds_control_dtc_setting_on_positive_response);
    RUN_TEST(test_uds_control_dtc_setting_invalid_subfunction_returns_nrc12);
    RUN_TEST(test_uds_ecu_reset_echoes_subfunction);
    RUN_TEST(test_uds_security_access_returns_nrc22);
    RUN_TEST(test_uds_routine_control_returns_nrc31);
    RUN_TEST(test_uds_read_dtc_info_returns_service_not_supported_in_fordiag_mode);
    RUN_TEST(test_kwp_read_dtc_by_status_reports_stored_code);
    RUN_TEST(test_kwp_read_dtc_by_status_reports_active_code);
    RUN_TEST(test_uds_read_memory_by_addr_unmapped_returns_zero_filled_block);
    RUN_TEST(test_uds_read_memory_by_addr_invalid_type_returns_general_negative);
    RUN_TEST(test_uds_read_ford_boost_did_returns_pressure_payload);
    RUN_TEST(test_uds_read_ecu_capabilities_returns_nrc31);
    RUN_TEST(test_uds_read_unknown_did_returns_nrc31);
    RUN_TEST(test_uds_read_scp_load_returns_scaled_word);
    RUN_TEST(test_uds_read_scp_bp_returns_default_barometric_value);
    RUN_TEST(test_uds_read_scp_idblock_addr_returns_descriptor);
    RUN_TEST(test_uds_read_f4_catch_code_alt_prefers_live_pid_payload);
    RUN_TEST(test_uds_read_scp_maf_pid_returns_nrc31);
    RUN_TEST(test_kwp_read_local_id_rom_size_returns_512k);
    RUN_TEST(test_kwp_read_local_id_compact_ident_returns_nrc31);

    // UDS short-frame -> NRC_INCORRECT_LENGTH regression guards
    RUN_TEST(test_uds_diag_session_short_frame_returns_nrc13);
    RUN_TEST(test_uds_read_data_by_id_short_frame_returns_nrc13);
    RUN_TEST(test_uds_read_memory_by_addr_short_frame_returns_nrc13);
    RUN_TEST(test_uds_control_dtc_setting_short_frame_returns_nrc13);

    RUN_TEST(test_stmin_to_ms_preserves_millisecond_values);
    RUN_TEST(test_stmin_to_ms_clamps_submillisecond_range_to_one_ms);
    RUN_TEST(test_stmin_to_ms_rejects_reserved_values);

    // Ford part number split
    RUN_TEST(test_ford_part_split_standard);
    RUN_TEST(test_ford_part_split_three_char_suffix);
    RUN_TEST(test_ford_part_split_no_dashes_fails);
    RUN_TEST(test_ford_part_split_one_dash_fails);

    // Ford suffix encoding
    RUN_TEST(test_ford_suffix_single_char_A);
    RUN_TEST(test_ford_suffix_single_char_B);
    RUN_TEST(test_ford_suffix_two_chars_AX);
    RUN_TEST(test_ford_suffix_two_chars_FA);

    // DTC payload
    RUN_TEST(test_fill_dtc_payload_empty);
    RUN_TEST(test_fill_dtc_payload_with_codes);
    RUN_TEST(test_fill_dtc_payload_null_safety);
    RUN_TEST(test_fill_dtc_payload_small_buffer);
    RUN_TEST(test_dtc_timestamp_first_occurrence_from_gps);
    RUN_TEST(test_dtc_timestamp_not_overwritten_on_reactivation);

    // TOTDIST
#ifdef OBD_ENABLE_TOTDIST
    RUN_TEST(test_totdist_default_value);
    RUN_TEST(test_totdist_set_and_get);
    RUN_TEST(test_totdist_set_zero);
    RUN_TEST(test_totdist_set_max_3byte);
#endif

    return UNITY_END();
}
