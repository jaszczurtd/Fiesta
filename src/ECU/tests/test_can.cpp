#include "can.h"
#include "dtcManager.h"
#include "hal/impl/.mock/hal_mock.h"
#include "rtcTime.h"
#include "sensors.h"
#include "unity.h"

#include <cstring>

static void ensure_can_ready(void) {
  if (canTestGetCanHandle() == NULL) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, canInit(1));
  }
  TEST_ASSERT_NOT_NULL(canTestGetCanHandle());
  hal_mock_can_reset(canTestGetCanHandle());
}

/* True when a sent frame was popped; asserts the mock had no other error. */
static bool pop_can_tx(uint32_t *id, uint8_t *len, uint8_t *data) {
  const hal_status_t st =
      hal_mock_can_get_sent(canTestGetCanHandle(), id, len, data);
  if (st != HAL_EAGAIN) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, st);
  }
  return st == HAL_OK;
}

#ifndef VP37
static void injectSupplyVoltageAdc(float volts) {
  const float ratio = ((float)JH_HW_NODE_VP37_PROP_SUPPLY_DIVIDER_HIGH_OHMS +
                       (float)JH_HW_NODE_VP37_PROP_SUPPLY_DIVIDER_LOW_OHMS) /
                      (float)JH_HW_NODE_VP37_PROP_SUPPLY_DIVIDER_LOW_OHMS;
  int adc = (int)((volts / ratio) * (4095.0f / 3.3f) + 0.5f);
  if (adc < 0) {
    adc = 0;
  }
  if (adc > 4095) {
    adc = 4095;
  }
  hal_mock_adc_inject(JH_HW_NODE_VP37_PIN_SUPPLY_ADC, adc);
}

static void refreshMediumValuesUntilVoltsUpdated(void) {
  for (int i = 0; i < (F_LAST + 2); i++) {
    readMediumValues();
    if (getGlobalValue(F_VOLTS) > 0.0f) {
      return;
    }
  }
}
#endif

void setUp(void) {
  initSensors();
  ensure_can_ready();
  hal_mock_can_set_state(canTestGetCanHandle(), HAL_CAN_STATE_ERROR_ACTIVE);
  hal_mock_set_millis(0u);
  canTestResetRpmPublisher();
  rtcTimeResetForTest();
  for (int32_t i = 0; i < F_LAST; i++) {
    setGlobalValue(i, 0.0f);
  }
}

void tearDown(void) { hal_mock_i2c_set_busy(false); }

void test_pack_gps_datetime_valid(void) {
  // 2026-04-03 14:27 -> yy=26 => offset=6
  // packed: [yearOff:4][month:4][day:5][hour:5][min:6]
  uint32_t packed = fiesta_can_pack_gps_datetime(260403, 1427);
  uint32_t expected = (6u << 20) | (4u << 16) | (3u << 11) | (14u << 6) | 27u;
  TEST_ASSERT_EQUAL_UINT32(expected, packed);
}

void test_pack_gps_datetime_invalid_returns_zero(void) {
  TEST_ASSERT_EQUAL_UINT32(
      0u, fiesta_can_pack_gps_datetime(261303, 1427)); // invalid month
  TEST_ASSERT_EQUAL_UINT32(
      0u, fiesta_can_pack_gps_datetime(260403, 2460)); // invalid time
}

/* 0x130 of Fiesta_clock: packed date and time to the minute, the seconds and
 * the integrity byte. */
static void inject_rtc_frame(const uint8_t *packed, uint8_t second,
                             uint8_t integrity) {
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  frame[CAN_FRAME_RTC_UPDATE_DT_HI] = packed[0];
  frame[CAN_FRAME_RTC_UPDATE_DT_MD] = packed[1];
  frame[CAN_FRAME_RTC_UPDATE_DT_LO] = packed[2];
  frame[CAN_FRAME_RTC_UPDATE_SECOND] = second;
  frame[CAN_FRAME_RTC_UPDATE_INTEGRITY] = integrity;
  hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_RTC_UPDATE,
                      CAN_FRAME_MAX_LENGTH, frame);
  canMainLoop();
}

// 2026-10-27 09:41 UTC, written out by hand from the field layout.
static const uint8_t kRtcPacked[3] = {0x6Au, 0xDAu, 0x69u};
static const uint32_t kRtcUtc = 1793094067u; // the same minute, second 7

void test_rtc_frame_gives_the_utc_time(void) {
  inject_rtc_frame(kRtcPacked, 7u, 1u);
  TEST_ASSERT_EQUAL_UINT32(kRtcUtc, rtcTimeGetUtc());

  // The encoder of Fiesta_clock and this decoder agree at the year limits.
  const uint32_t limits[] = {1577836800u,  // 2020-01-01 00:00:00
                             2082758399u}; // 2035-12-31 23:59:59
  for (size_t i = 0; i < COUNTOF(limits); i++) {
    uint8_t packed[3] = {0};
    TEST_ASSERT_TRUE(fiesta_can_put_utc_datetime(packed, limits[i]));
    inject_rtc_frame(packed, (uint8_t)(limits[i] % 60u), 1u);
    TEST_ASSERT_EQUAL_UINT32(limits[i], rtcTimeGetUtc());
  }
}

void test_rtc_frame_without_integrity_or_with_a_bad_date_clears_the_time(void) {
  inject_rtc_frame(kRtcPacked, 7u, 1u);
  inject_rtc_frame(kRtcPacked, 7u, 0u);
  TEST_ASSERT_EQUAL_UINT32(0u, rtcTimeGetUtc());

  const uint8_t zero[3] = {0u, 0u, 0u};
  const uint8_t month13[3] = {0x6Du, 0xDAu, 0x69u};
  const uint8_t feb30[3] = {0x62u, 0xF2u, 0x69u};
  const uint8_t *bad[] = {zero, month13, feb30};
  for (size_t i = 0; i < COUNTOF(bad); i++) {
    inject_rtc_frame(kRtcPacked, 7u, 1u);
    inject_rtc_frame(bad[i], 7u, 1u);
    TEST_ASSERT_EQUAL_UINT32(0u, rtcTimeGetUtc());
  }
  inject_rtc_frame(kRtcPacked, 7u, 1u);
  inject_rtc_frame(kRtcPacked, 60u, 1u);
  TEST_ASSERT_EQUAL_UINT32(0u, rtcTimeGetUtc());
}

void test_truncated_rtc_frame_is_ignored(void) {
  inject_rtc_frame(kRtcPacked, 7u, 1u);
  uint8_t shortFrame[CAN_FRAME_RTC_UPDATE_INTEGRITY] = {0};
  hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_RTC_UPDATE,
                      (uint8_t)sizeof(shortFrame), shortFrame);
  canMainLoop();
  TEST_ASSERT_EQUAL_UINT32(kRtcUtc, rtcTimeGetUtc());
}

void test_connection_check_drops_a_silent_rtc(void) {
  inject_rtc_frame(kRtcPacked, 7u, 1u);
  hal_mock_advance_millis(RTC_TIME_MAX_AGE_MS);
  canCheckConnection();
  // Even when the tick counter comes back to the reception time.
  hal_mock_set_millis(0u);
  TEST_ASSERT_EQUAL_UINT32(0u, rtcTimeGetUtc());
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
  hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_OIL_AND_SPEED_MODULE_UPDATE,
                      2, shortFrame);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(4.2f, getGlobalValue(F_OIL_PRESSURE));
  TEST_ASSERT_EQUAL_FLOAT(88.0f, getGlobalValue(F_ABS_CAR_SPEED));
}

void test_egt_frame_decodes_negative_temperatures_and_validity(void) {
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  TEST_ASSERT_TRUE(
      fiesta_can_put_temp_i16(&frame[CAN_FRAME_EGT_UPDATE_EGT_HI], -21.0f));
  TEST_ASSERT_TRUE(fiesta_can_put_temp_i16(
      &frame[CAN_FRAME_EGT_UPDATE_DPF_TEMP_HI], 655.0f));
  frame[CAN_FRAME_EGT_UPDATE_STATUS] =
      CAN_EGT_STATUS_EGT_VALID | CAN_EGT_STATUS_DPF_TEMP_VALID;
  hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_EGT_UPDATE,
                      CAN_FRAME_MAX_LENGTH, frame);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(-21.0f, getGlobalValue(F_EGT));
  TEST_ASSERT_EQUAL_FLOAT(655.0f, getGlobalValue(F_DPF_TEMP));
}

void test_egt_channel_without_valid_bit_has_no_measurement(void) {
  setGlobalValue(F_EGT, 321.0f);
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  (void)fiesta_can_put_temp_i16(&frame[CAN_FRAME_EGT_UPDATE_EGT_HI], 700.0f);
  (void)fiesta_can_put_temp_i16(&frame[CAN_FRAME_EGT_UPDATE_DPF_TEMP_HI],
                                410.0f);
  frame[CAN_FRAME_EGT_UPDATE_STATUS] = CAN_EGT_STATUS_DPF_TEMP_VALID;
  hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_EGT_UPDATE,
                      CAN_FRAME_MAX_LENGTH, frame);
  canMainLoop();

  TEST_ASSERT_EQUAL_FLOAT(0.0f, getGlobalValue(F_EGT));
  TEST_ASSERT_EQUAL_FLOAT(410.0f, getGlobalValue(F_DPF_TEMP));
}

void test_oil_pressure_without_valid_bit_has_no_measurement(void) {
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  frame[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_HI] = 3;
  frame[CAN_FRAME_ECU_UPDATE_OIL_PRESSURE_LO] = 5;
  frame[CAN_FRAME_ECU_UPDATE_ABS_CAR_SPEED] = 64;
  frame[CAN_FRAME_OIL_AND_SPEED_STATUS] = CAN_OIL_AND_SPEED_STATUS_OIL_VALID;
  hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_OIL_AND_SPEED_MODULE_UPDATE,
                      CAN_FRAME_MAX_LENGTH, frame);
  canMainLoop();
  TEST_ASSERT_EQUAL_FLOAT(3.5f, getGlobalValue(F_OIL_PRESSURE));
  TEST_ASSERT_EQUAL_FLOAT(64.0f, getGlobalValue(F_ABS_CAR_SPEED));

  frame[CAN_FRAME_OIL_AND_SPEED_STATUS] = 0u;
  hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_OIL_AND_SPEED_MODULE_UPDATE,
                      CAN_FRAME_MAX_LENGTH, frame);
  canMainLoop();
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getGlobalValue(F_OIL_PRESSURE));
  TEST_ASSERT_EQUAL_FLOAT(64.0f, getGlobalValue(F_ABS_CAR_SPEED));
}

// A valid bit counts only while frames of its kind keep arriving.
void test_measurement_validity_needs_the_flag_and_fresh_frames(void) {
  canCheckConnection(); // forget the frames of earlier tests
  canCheckConnection();
  TEST_ASSERT_FALSE(isEGTValid());
  TEST_ASSERT_FALSE(isOilPressureValid());

  uint8_t egt[CAN_FRAME_MAX_LENGTH] = {0};
  egt[CAN_FRAME_EGT_UPDATE_STATUS] = CAN_EGT_STATUS_EGT_VALID;
  hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_EGT_UPDATE,
                      CAN_FRAME_MAX_LENGTH, egt);
  uint8_t oil[CAN_FRAME_MAX_LENGTH] = {0};
  oil[CAN_FRAME_OIL_AND_SPEED_STATUS] = CAN_OIL_AND_SPEED_STATUS_OIL_VALID;
  hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_OIL_AND_SPEED_MODULE_UPDATE,
                      CAN_FRAME_MAX_LENGTH, oil);
  canMainLoop();
  canCheckConnection();
  TEST_ASSERT_TRUE(isEGTValid());
  TEST_ASSERT_FALSE(isDPFTempValid());
  TEST_ASSERT_TRUE(isOilPressureValid());

  oil[CAN_FRAME_OIL_AND_SPEED_STATUS] = 0u;
  hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_OIL_AND_SPEED_MODULE_UPDATE,
                      CAN_FRAME_MAX_LENGTH, oil);
  canMainLoop();
  TEST_ASSERT_FALSE(isOilPressureValid());

  canCheckConnection(); // no EGT frame since the previous check
  TEST_ASSERT_FALSE(isEGTValid());
}

static void assert_rpm_frame(int32_t expectedRpm) {
  uint32_t id = 0u;
  uint8_t len = 0u;
  uint8_t data[CAN_FRAME_MAX_LENGTH] = {0};

  TEST_ASSERT_TRUE(pop_can_tx(&id, &len, data));
  TEST_ASSERT_EQUAL_UINT32(CAN_ID_RPM, id);
  TEST_ASSERT_EQUAL_UINT8(CAN_FRAME_MAX_LENGTH, len);
  TEST_ASSERT_EQUAL_INT(expectedRpm,
                        jh_u16_from_bytes(data[CAN_FRAME_RPM_UPDATE_HI],
                                          data[CAN_FRAME_RPM_UPDATE_LO]));
}

void test_can_init_allows_hardware_arbitration_retry(void) {
  hal_can_mode_t mode = HAL_CAN_MODE_NORMAL;

  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_can_get_mode(canTestGetCanHandle(), &mode));
  TEST_ASSERT_EQUAL(0u, mode & HAL_CAN_MODE_ONE_SHOT);
}

void test_rpm_update_retries_after_failed_send(void) {
  getRPMInstance()->rpmValue = 1234;
  hal_mock_can_set_state(canTestGetCanHandle(), HAL_CAN_STATE_BUS_OFF);

  CAN_updaterecipients_02();

  uint32_t id = 0u;
  uint8_t len = 0u;
  uint8_t data[CAN_FRAME_MAX_LENGTH] = {0};
  TEST_ASSERT_FALSE(pop_can_tx(&id, &len, data));

  hal_mock_can_set_state(canTestGetCanHandle(), HAL_CAN_STATE_ERROR_ACTIVE);
  hal_mock_advance_millis(CAN_RPM_RETRY_INTERVAL_MS - 1u);
  getRPMInstance()->rpmValue = 1300;
  CAN_updaterecipients_02();
  TEST_ASSERT_FALSE(pop_can_tx(&id, &len, data));

  hal_mock_advance_millis(1u);
  CAN_updaterecipients_02();
  assert_rpm_frame(1300);
}

static bool bus_fault_active(void) {
  uint16_t codes[12] = {0};
  const uint8_t n = dtcManagerGetCodes(DTC_KIND_ACTIVE, codes, 12);
  for (uint8_t i = 0; i < n; i++) {
    if (codes[i] == DTC_CAN_BUS_FAULT) {
      return true;
    }
  }
  return false;
}

void test_check_connection_reports_bus_off_with_iso_detail(void) {
  hal_mock_can_set_state(canTestGetCanHandle(), HAL_CAN_STATE_BUS_OFF);
  canCheckConnection();
  TEST_ASSERT_TRUE(bus_fault_active());
  TEST_ASSERT_EQUAL_UINT8(DTC_DETAIL_CAN_BUS_OFF,
                          dtcManagerGetDetail(DTC_CAN_BUS_FAULT));

  // One clean check period clears the active flag; the detail stays.
  hal_mock_can_set_state(canTestGetCanHandle(), HAL_CAN_STATE_ERROR_ACTIVE);
  canCheckConnection();
  TEST_ASSERT_FALSE(bus_fault_active());
  TEST_ASSERT_EQUAL_UINT8(DTC_DETAIL_CAN_BUS_OFF,
                          dtcManagerGetDetail(DTC_CAN_BUS_FAULT));
}

void test_check_connection_names_the_frame_whose_send_failed(void) {
  // A stopped controller refuses every send without reporting bus-off.
  hal_mock_can_set_state(canTestGetCanHandle(), HAL_CAN_STATE_STOPPED);
  getRPMInstance()->rpmValue = 1234;
  CAN_updaterecipients_02();

  canCheckConnection();
  TEST_ASSERT_TRUE(bus_fault_active());
  TEST_ASSERT_EQUAL_UINT8(DTC_DETAIL_CAN_TX_RPM,
                          dtcManagerGetDetail(DTC_CAN_BUS_FAULT));

  hal_mock_can_set_state(canTestGetCanHandle(), HAL_CAN_STATE_ERROR_ACTIVE);
  canCheckConnection();
  TEST_ASSERT_FALSE(bus_fault_active());
}

void test_rpm_update_sends_periodic_heartbeat(void) {
  getRPMInstance()->rpmValue = 2345;

  CAN_updaterecipients_02();
  assert_rpm_frame(2345);

  CAN_updaterecipients_02();
  uint32_t id = 0u;
  uint8_t len = 0u;
  uint8_t data[CAN_FRAME_MAX_LENGTH] = {0};
  TEST_ASSERT_FALSE(pop_can_tx(&id, &len, data));

  getRPMInstance()->rpmValue = 2400;
  CAN_updaterecipients_02();
  assert_rpm_frame(2400);

  hal_mock_advance_millis(CAN_RPM_HEARTBEAT_INTERVAL_MS - 1u);
  CAN_updaterecipients_02();
  TEST_ASSERT_FALSE(pop_can_tx(&id, &len, data));

  hal_mock_advance_millis(1u);
  CAN_updaterecipients_02();
  assert_rpm_frame(2400);
}

#ifndef VP37
void test_can_update_01_contains_adc_supply_voltage_without_vp37(void) {
  hal_mock_i2c_set_busy(
      true); // Ensure this path does not depend on Adjustometer I2C.
  injectSupplyVoltageAdc(13.8f);

  setGlobalValue(F_VOLTS, 0.0f);
  refreshMediumValuesUntilVoltsUpdated();

  float measuredVolts = getGlobalValue(F_VOLTS);
  TEST_ASSERT_TRUE(measuredVolts > 1.0f);

  CAN_updaterecipients_01();
  canMainLoop();

  uint32_t id = 0;
  uint8_t len = 0;
  uint8_t data[CAN_FRAME_MAX_LENGTH] = {0};

  TEST_ASSERT_TRUE(pop_can_tx(&id, &len, data));
  TEST_ASSERT_EQUAL_UINT32(CAN_ID_ECU_UPDATE_01, id);
  TEST_ASSERT_EQUAL_UINT8(CAN_FRAME_MAX_LENGTH, len);

  int hi = 0;
  int lo = 0;
  fiesta_can_split_decimal_tenths(measuredVolts, &hi, &lo);

  TEST_ASSERT_EQUAL_UINT8((uint8_t)hi, data[CAN_FRAME_ECU_UPDATE_VOLTS_HI]);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)lo, data[CAN_FRAME_ECU_UPDATE_VOLTS_LO]);
}
#endif

/* A controller that does not come up: canInit() says why, CAN0 stays off and
 * the periodic sends do nothing until a later init works. */
void test_can_init_reports_why_can0_stayed_off(void) {
  hal_can_destroy(canTestGetCanHandle());
  hal_mock_can_fail_creates(2u);
  TEST_ASSERT_EQUAL_INT(HAL_EIO, canInit(2));
  TEST_ASSERT_NULL(canTestGetCanHandle());
  CAN_sendAll();
  canMainLoop();
  hal_mock_can_fail_creates(0u);
  TEST_ASSERT_EQUAL_INT(HAL_OK, canInit(1));
  TEST_ASSERT_NOT_NULL(canTestGetCanHandle());
}

/* GPS frames of Clocks and air temperatures of Fiesta_clock reach the ECU,
 * which has no CAN0 filter; it skips them without a report. A frame of an
 * unknown ID is still reported. */
void test_frames_of_other_modules_are_skipped_quietly(void) {
  ensure_can_ready();
  const uint8_t frame[CAN_FRAME_MAX_LENGTH] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
  const uint32_t ids[] = {CAN_ID_GPS_EXT_LAT, CAN_ID_GPS_EXT_LON_TIME,
                          CAN_ID_GPS_UTC_UPDATE, CAN_ID_AIR_TEMP_UPDATE};
  // Past the rate limit of earlier reports, so a report would print.
  hal_mock_set_millis(hal_millis() + 3600000u);
  hal_mock_serial_reset();
  for (size_t i = 0u; i < COUNTOF(ids); i++) {
    hal_mock_can_inject(canTestGetCanHandle(), ids[i], CAN_FRAME_MAX_LENGTH,
                        frame);
  }
  canMainLoop();
  TEST_ASSERT_EQUAL_STRING("", hal_mock_serial_last_line());

  hal_mock_can_inject(canTestGetCanHandle(), 0x13Fu, CAN_FRAME_MAX_LENGTH,
                      frame);
  canMainLoop();
  TEST_ASSERT_NOT_NULL(
      strstr(hal_mock_serial_last_line(), "unknown CAN frame:13f"));
}

/* GPS moved to Clocks: 0x129 keeps intake and fuel with the withdrawn GPS
 * bytes zero, also after 0x123 filled the same buffer positions, and the
 * periodic set sends neither 0x12D nor 0x12E. */
void test_ecu_frames_carry_no_gps(void) {
  setGlobalValue(F_COOLANT_TEMP, 90.0f);
  setGlobalValue(F_OIL_TEMP, 100.0f);
  setGlobalValue(F_INTAKE_TEMP, 25.0f);
  setGlobalValue(F_FUEL, 300.0f);
  setGlobalValue(F_GPS_IS_AVAILABLE, 1.0f);
  setGlobalValue(F_GPS_CAR_SPEED, 77.0f);

  CAN_sendAll();

  uint32_t id = 0;
  uint8_t len = 0;
  uint8_t data[CAN_FRAME_MAX_LENGTH] = {0};
  bool sawUpdate02 = false;
  while (pop_can_tx(&id, &len, data)) {
    TEST_ASSERT_NOT_EQUAL_UINT32(CAN_ID_GPS_EXT_LAT, id);
    TEST_ASSERT_NOT_EQUAL_UINT32(CAN_ID_GPS_EXT_LON_TIME, id);
    if (id == CAN_ID_ECU_UPDATE_01) {
      TEST_ASSERT_NOT_EQUAL_UINT8(0u, data[CAN_FRAME_ECU_UPDATE_COOLANT]);
      TEST_ASSERT_NOT_EQUAL_UINT8(0u, data[CAN_FRAME_ECU_UPDATE_OIL]);
    }
    if (id == CAN_ID_ECU_UPDATE_02) {
      sawUpdate02 = true;
      TEST_ASSERT_EQUAL_UINT8(hal_can_encode_temp_i8(25.0f),
                              data[CAN_FRAME_ECU_UPDATE_INTAKE]);
      TEST_ASSERT_EQUAL_UINT16(
          300u, jh_u16_from_bytes(data[CAN_FRAME_ECU_UPDATE_FUEL_HI],
                                  data[CAN_FRAME_ECU_UPDATE_FUEL_LO]));
      TEST_ASSERT_EQUAL_UINT8(0u, data[4u]);
      TEST_ASSERT_EQUAL_UINT8(0u, data[5u]);
    }
  }
  TEST_ASSERT_TRUE(sawUpdate02);
}

static uint8_t expect_sent_frame(uint32_t expectedId, uint8_t *payload) {
  uint32_t id = 0u;
  uint8_t length = 0u;
  TEST_ASSERT_TRUE(pop_can_tx(&id, &length, payload));
  TEST_ASSERT_EQUAL_UINT32(expectedId, id);
  TEST_ASSERT_EQUAL_UINT8(CAN_FRAME_MAX_LENGTH, length);
  return payload[CAN_FRAME_NUMBER];
}

static void expect_no_sent_frame(void) {
  uint32_t id = 0u;
  uint8_t length = 0u;
  uint8_t payload[CAN_FRAME_MAX_LENGTH] = {0};
  TEST_ASSERT_FALSE(pop_can_tx(&id, &length, payload));
}

void test_periodic_service_sends_a_due_frame_without_running_rx_callbacks(
    void) {
  setGlobalValue(F_EGT, 321.0f);
  uint8_t frame[CAN_FRAME_MAX_LENGTH] = {0};
  TEST_ASSERT_TRUE(
      fiesta_can_put_temp_i16(&frame[CAN_FRAME_EGT_UPDATE_EGT_HI], -21.0f));
  frame[CAN_FRAME_EGT_UPDATE_STATUS] = CAN_EGT_STATUS_EGT_VALID;
  hal_mock_can_inject(canTestGetCanHandle(), CAN_ID_EGT_UPDATE,
                      CAN_FRAME_MAX_LENGTH, frame);
  hal_mock_set_millis(100u);
  CAN_updaterecipients_01();
  uint8_t payload[CAN_FRAME_MAX_LENGTH] = {0};
  const uint8_t first = expect_sent_frame(CAN_ID_ECU_UPDATE_01, payload);
  hal_mock_set_millis(102u);
  canServicePeriodic();
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(first + 1u),
                          expect_sent_frame(CAN_ID_ECU_UPDATE_02, payload));
  TEST_ASSERT_EQUAL_FLOAT(321.0f, getGlobalValue(F_EGT));
  canMainLoop();
  TEST_ASSERT_EQUAL_FLOAT(-21.0f, getGlobalValue(F_EGT));
  expect_no_sent_frame();
}

void test_periodic_group_spreads_frames_and_numbers_rpm_in_wire_order(void) {
  hal_mock_set_millis(100u);
  CAN_updaterecipients_01();
  canMainLoop();
  uint8_t payload[CAN_FRAME_MAX_LENGTH] = {0};
  const uint8_t first = expect_sent_frame(CAN_ID_ECU_UPDATE_01, payload);
  canMainLoop();
  hal_mock_set_millis(101u);
  canMainLoop();
  expect_no_sent_frame();
  hal_mock_set_millis(102u);
  CAN_updaterecipients_02();
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(first + 1u),
                          expect_sent_frame(CAN_ID_RPM, payload));
  canMainLoop();
  expect_no_sent_frame();
  hal_mock_set_millis(104u);
  canMainLoop();
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(first + 2u),
                          expect_sent_frame(CAN_ID_ECU_UPDATE_02, payload));
  hal_mock_set_millis(106u);
  canMainLoop();
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(first + 3u),
                          expect_sent_frame(CAN_ID_ECU_UPDATE_03, payload));
  hal_mock_set_millis(108u);
  canMainLoop();
  expect_no_sent_frame();
}

void test_deferred_group_keeps_snapshot_and_coalesces_without_sequence_holes(
    void) {
  hal_mock_set_millis(100u);
  setGlobalValue(F_INTAKE_TEMP, 25.0f);
  CAN_updaterecipients_01();
  canMainLoop();
  uint8_t payload[CAN_FRAME_MAX_LENGTH] = {0};
  const uint8_t first = expect_sent_frame(CAN_ID_ECU_UPDATE_01, payload);
  setGlobalValue(F_INTAKE_TEMP, 99.0f);
  hal_mock_set_millis(102u);
  canMainLoop();
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(first + 1u),
                          expect_sent_frame(CAN_ID_ECU_UPDATE_02, payload));
  TEST_ASSERT_EQUAL_UINT8(hal_can_encode_temp_i8(25.0f),
                          payload[CAN_FRAME_ECU_UPDATE_INTAKE]);
  hal_mock_set_millis(103u);
  setGlobalValue(F_INTAKE_TEMP, 35.0f);
  CAN_updaterecipients_01();
  canMainLoop();
  expect_no_sent_frame();
  hal_mock_set_millis(104u);
  canMainLoop();
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(first + 2u),
                          expect_sent_frame(CAN_ID_ECU_UPDATE_03, payload));
  hal_mock_set_millis(106u);
  canMainLoop();
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(first + 3u),
                          expect_sent_frame(CAN_ID_ECU_UPDATE_01, payload));
  hal_mock_set_millis(108u);
  canMainLoop();
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(first + 4u),
                          expect_sent_frame(CAN_ID_ECU_UPDATE_02, payload));
  TEST_ASSERT_EQUAL_UINT8(hal_can_encode_temp_i8(35.0f),
                          payload[CAN_FRAME_ECU_UPDATE_INTAKE]);
  expect_no_sent_frame();
}

void test_deferred_group_spacing_survives_millis_wrap(void) {
  hal_mock_set_millis(UINT32_MAX - 1u);
  CAN_updaterecipients_01();
  canMainLoop();
  uint8_t payload[CAN_FRAME_MAX_LENGTH] = {0};
  (void)expect_sent_frame(CAN_ID_ECU_UPDATE_01, payload);
  hal_mock_set_millis(UINT32_MAX);
  canMainLoop();
  expect_no_sent_frame();
  hal_mock_set_millis(0u);
  canMainLoop();
  (void)expect_sent_frame(CAN_ID_ECU_UPDATE_02, payload);
  hal_mock_set_millis(1u);
  canMainLoop();
  expect_no_sent_frame();
  hal_mock_set_millis(2u);
  canMainLoop();
  (void)expect_sent_frame(CAN_ID_ECU_UPDATE_03, payload);
  expect_no_sent_frame();
}

void test_periodic_publisher_keeps_every_rate_with_frequent_rpm_and_sequence_wrap(
    void) {
  const uint32_t ids[] = {CAN_ID_ECU_UPDATE_01, CAN_ID_ECU_UPDATE_02,
                          CAN_ID_ECU_UPDATE_03, CAN_ID_THROTTLE,
                          CAN_ID_TURBO_PRESSURE};
  uint32_t counts[COUNTOF(ids)] = {0u};
  uint32_t rpmCount = 0u;
  bool haveSequence = false;
  uint8_t nextSequence = 0u;
  for (uint32_t ms = 0u; ms <= 5020u; ++ms) {
    hal_mock_set_millis(ms);
    getRPMInstance()->rpmValue =
        1000 + (int32_t)((ms < 5000u ? ms : 5000u) / 10u);
    if (ms < 5000u && (ms % 50u) == 0u) {
      CAN_updaterecipients_01();
      CAN_sendThrottleUpdate();
      CAN_sendTurboUpdate();
    }
    CAN_updaterecipients_02();
    if ((ms % 4u) == 0u) {
      canMainLoop();
    }
    uint32_t id = 0u;
    uint8_t length = 0u;
    uint8_t payload[CAN_FRAME_MAX_LENGTH] = {0};
    while (pop_can_tx(&id, &length, payload)) {
      if (haveSequence) {
        TEST_ASSERT_EQUAL_UINT8(nextSequence, payload[CAN_FRAME_NUMBER]);
      }
      nextSequence = (uint8_t)(payload[CAN_FRAME_NUMBER] + 1u);
      haveSequence = true;
      if (id == CAN_ID_RPM) {
        rpmCount++;
      } else {
        bool found = false;
        for (size_t i = 0u; i < COUNTOF(ids); ++i) {
          if (id == ids[i]) {
            counts[i]++;
            found = true;
            break;
          }
        }
        TEST_ASSERT_TRUE(found);
      }
    }
  }
  for (size_t i = 0u; i < COUNTOF(counts); ++i) {
    TEST_ASSERT_EQUAL_UINT32(100u, counts[i]);
  }
  TEST_ASSERT_EQUAL_UINT32(501u, rpmCount);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(
      test_periodic_service_sends_a_due_frame_without_running_rx_callbacks);
  RUN_TEST(
      test_periodic_publisher_keeps_every_rate_with_frequent_rpm_and_sequence_wrap);
  RUN_TEST(test_periodic_group_spreads_frames_and_numbers_rpm_in_wire_order);
  RUN_TEST(
      test_deferred_group_keeps_snapshot_and_coalesces_without_sequence_holes);
  RUN_TEST(test_deferred_group_spacing_survives_millis_wrap);
  RUN_TEST(test_pack_gps_datetime_valid);
  RUN_TEST(test_pack_gps_datetime_invalid_returns_zero);
  RUN_TEST(test_rtc_frame_gives_the_utc_time);
  RUN_TEST(test_rtc_frame_without_integrity_or_with_a_bad_date_clears_the_time);
  RUN_TEST(test_truncated_rtc_frame_is_ignored);
  RUN_TEST(test_connection_check_drops_a_silent_rtc);
  RUN_TEST(test_truncated_egt_frame_is_ignored);
  RUN_TEST(test_truncated_oil_and_speed_frame_is_ignored);
  RUN_TEST(test_egt_frame_decodes_negative_temperatures_and_validity);
  RUN_TEST(test_egt_channel_without_valid_bit_has_no_measurement);
  RUN_TEST(test_oil_pressure_without_valid_bit_has_no_measurement);
  RUN_TEST(test_measurement_validity_needs_the_flag_and_fresh_frames);
  RUN_TEST(test_can_init_allows_hardware_arbitration_retry);
  RUN_TEST(test_rpm_update_retries_after_failed_send);
  RUN_TEST(test_rpm_update_sends_periodic_heartbeat);
  RUN_TEST(test_check_connection_reports_bus_off_with_iso_detail);
  RUN_TEST(test_check_connection_names_the_frame_whose_send_failed);
#ifndef VP37
  RUN_TEST(test_can_update_01_contains_adc_supply_voltage_without_vp37);
#endif
  RUN_TEST(test_can_init_reports_why_can0_stayed_off);
  RUN_TEST(test_ecu_frames_carry_no_gps);
  RUN_TEST(test_frames_of_other_modules_are_skipped_quietly);
  return UNITY_END();
}
