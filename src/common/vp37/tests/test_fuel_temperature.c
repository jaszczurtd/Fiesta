#include "can.h"
#include "config.h"
#include "dtcManager.h"
#include "obd-2.h"
#include "obd_protocol.h"
#include "sensors.h"
#include "tests/testable/obd2_testable.h"
#include "unity.h"
#include "vp37_adapter.h"
#include "vp37_pump_setup.h"
#include <hal/usb/hal_usb.h>
#include <math.h>
#include <string.h>

/* After the HAL configuration: the CAN part of the bridge needs it. */
#include "../../tests/hal_mock_c_bridge.h"

static VP37Pump pump;
static uint8_t frame[ADJUSTOMETER_FEEDBACK_BYTES];
static int32_t writtenPwm;
static bool transferFails;

static void writePwm(int32_t value) { writtenPwm = value; }
static void enableDrive(bool enabled) { (void)enabled; }
static bool driveEnabled(void) { return true; }
static hal_status_t transfer(uint8_t reg, uint8_t *out, size_t length) {
  if (transferFails || (reg != ADJUSTOMETER_FEEDBACK_START) ||
      (length != COUNTOF(frame))) {
    return HAL_EBUS;
  }
  memcpy(out, frame, length);
  return HAL_OK;
}

void setUp(void) {
  fiesta_test_set_millis(0U);
  transferFails = false;
  vp37AdapterResetForTest();
  dtcManagerClearAll();
  initSensors();
  static const VP37Callbacks callbacks = {
      .writeQuantityPwm = writePwm,
      .writeTimingPwm = writePwm,
      .setDriveEnabled = enableDrive,
      .driveEnabled = driveEnabled,
      .adjustometerTransfer = transfer,
  };
  vp37SetupCalibratedPump(&pump, &callbacks);
  const float ratio = ((float)JH_HW_NODE_VP37_PROP_SUPPLY_DIVIDER_HIGH_OHMS +
                       (float)JH_HW_NODE_VP37_PROP_SUPPLY_DIVIDER_LOW_OHMS) /
                      (float)JH_HW_NODE_VP37_PROP_SUPPLY_DIVIDER_LOW_OHMS;
  fiesta_test_adc_inject(JH_HW_NODE_VP37_PIN_SUPPLY_ADC,
                         (int)(14.0f / ratio * 4095.0f / 3.3f + 0.5f));
  VP37_setPositionDemandPercentage(&pump, 50.0f);
}

void tearDown(void) {
  hal_pid_controller_destroy(pump.pid.controller);
  pump.pid.controller = NULL;
}

static void processTemperature(int16_t temperature, uint8_t status,
                               uint8_t version) {
  transferFails = false;
  fiesta_test_set_millis(hal_millis() + 5U);
  adjustometer_feedback_t sample =
      vp37TestFeedback(4500, 144, temperature, status);
  adjustometer_feedback_encode(frame, &sample, 2U);
  frame[0] = version;
  if (version == 1U) {
    frame[5] = (uint8_t)temperature;
  }
  VP37_process(&pump);
}

static void test_cold_data_preserves_legacy_drive_policy(void) {
  processTemperature(0, ADJ_STATUS_FUEL_TEMP_BROKEN, 1U);
  const int32_t legacyPwm = pump.output.finalPWM;
  const float legacyLimit = pump.pid.positiveLimit;
  TEST_ASSERT_EQUAL_FLOAT(1.0f, pump.thermal.temperatureCorrection);
  const int16_t temperatures[] = {-40, -20, 0};
  for (size_t i = 0U; i < COUNTOF(temperatures); i++) {
    tearDown();
    setUp();
    processTemperature(temperatures[i], ADJ_STATUS_OK, 2U);
    TEST_ASSERT_EQUAL_INT16(temperatures[i], pump.adjustometer.last.fuelTempC);
    TEST_ASSERT_EQUAL_FLOAT(temperatures[i], pump.feedback.fuelTempC);
    TEST_ASSERT_BITS_LOW(ADJ_STATUS_FUEL_TEMP_BROKEN, pump.feedback.lastStatus);
    TEST_ASSERT_FALSE(pump.thermal.temperatureReady);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, pump.thermal.temperatureCorrection);
    TEST_ASSERT_EQUAL_FLOAT(legacyLimit, pump.pid.positiveLimit);
    TEST_ASSERT_EQUAL_INT32(legacyPwm, pump.output.finalPWM);
    TEST_ASSERT_EQUAL_INT32(pump.output.finalPWM, writtenPwm);
    VP37Status status;
    TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readStatus(&pump, &status));
    TEST_ASSERT_TRUE(status.fuelTempValid);
    TEST_ASSERT_FALSE(status.fuelTempFault);
    TEST_ASSERT_EQUAL_FLOAT(temperatures[i], status.fuelTempC);
  }
}

static void test_positive_readings_match_both_protocols(void) {
  const int16_t temperatures[] = {1, 29, 49, 120};
  for (size_t i = 0U; i < COUNTOF(temperatures); i++) {
    tearDown();
    setUp();
    processTemperature(temperatures[i], ADJ_STATUS_OK, 1U);
    const int32_t legacyPwm = pump.output.finalPWM;
    const float legacyFactor = pump.thermal.temperatureCorrection;
    const float legacyLimit = pump.pid.positiveLimit;
    tearDown();
    setUp();
    processTemperature(temperatures[i], ADJ_STATUS_OK, 2U);
    TEST_ASSERT_TRUE(pump.thermal.temperatureReady);
    TEST_ASSERT_EQUAL_FLOAT(legacyFactor, pump.thermal.temperatureCorrection);
    TEST_ASSERT_EQUAL_FLOAT(legacyLimit, pump.pid.positiveLimit);
    TEST_ASSERT_EQUAL_INT32(legacyPwm, pump.output.finalPWM);
  }
}

static void
test_cold_and_invalid_hold_correction_but_report_distinct_status(void) {
  processTemperature(29, ADJ_STATUS_OK, 2U);
  const float factor = pump.thermal.temperatureCorrection;
  const int16_t temperatures[] = {-20, 0, 40};
  for (size_t i = 0U; i < COUNTOF(temperatures); i++) {
    processTemperature(temperatures[i],
                       i == 2U ? ADJ_STATUS_FUEL_TEMP_BROKEN : 0U, 2U);
    TEST_ASSERT_EQUAL_FLOAT(factor, pump.thermal.temperatureCorrection);
    TEST_ASSERT_EQUAL_FLOAT(VP37_PID_CORR_LIMIT_POSITIVE_COLD,
                            pump.pid.positiveLimit);
    VP37Status status;
    TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readStatus(&pump, &status));
    TEST_ASSERT_EQUAL_INT(i != 2U, status.fuelTempValid);
    TEST_ASSERT_EQUAL_INT(i == 2U, status.fuelTempFault);
  }
  processTemperature(49, ADJ_STATUS_OK, 2U);
  TEST_ASSERT_GREATER_THAN_FLOAT(factor, pump.thermal.temperatureCorrection);
  TEST_ASSERT_LESS_THAN_FLOAT(1.0f, pump.thermal.temperatureCorrection);
  TEST_ASSERT_GREATER_THAN_FLOAT(VP37_PID_CORR_LIMIT_POSITIVE_COLD,
                                 pump.pid.positiveLimit);
}

/* One v2 frame through the pump and what core 0 takes from it. */
static void publishTemperature(int16_t temperature, uint8_t status) {
  processTemperature(temperature, status, 2U);
  vp37AdapterPublish(&pump);
}

/* One control step without an answer from the Adjustometer. */
static void publishLostFrame(void) {
  transferFails = true;
  fiesta_test_set_millis(hal_millis() + 5U);
  VP37_process(&pump);
  vp37AdapterPublish(&pump);
}

static bool fuelSensorCodeIs(dtc_kind_t kind) {
  uint16_t codes[16];
  const uint8_t count =
      dtcManagerGetCodes(kind, codes, (uint8_t)COUNTOF(codes));
  for (uint8_t i = 0U; i < count; i++) {
    if (codes[i] == DTC_ADJ_FUEL_TEMP_BROKEN) {
      return true;
    }
  }
  return false;
}

static void test_ecu_globals_drop_invalid_temperature(void) {
  vp37AdapterPublish(&pump);
  TEST_ASSERT_FALSE(isFuelTempValid()); /* No frame yet. */
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getGlobalValue(F_FUEL_TEMP));
  publishTemperature(-20, ADJ_STATUS_OK);
  TEST_ASSERT_TRUE(isFuelTempValid());
  TEST_ASSERT_EQUAL_FLOAT(-20.0f, getGlobalValue(F_FUEL_TEMP));
  /* The frame still carries a number; the sensor status makes it invalid. */
  publishTemperature(25, ADJ_STATUS_FUEL_TEMP_BROKEN);
  TEST_ASSERT_FALSE(isFuelTempValid());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getGlobalValue(F_FUEL_TEMP));
  publishTemperature(31, ADJ_STATUS_OK);
  TEST_ASSERT_TRUE(isFuelTempValid());
  TEST_ASSERT_EQUAL_FLOAT(31.0f, getGlobalValue(F_FUEL_TEMP));
  /* A lost link invalidates the value at once, without a new frame. */
  publishLostFrame();
  TEST_ASSERT_FALSE(isFuelTempValid());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getGlobalValue(F_FUEL_TEMP));
}

static void test_fuel_sensor_code_confirms_after_window_and_clears(void) {
  publishTemperature(30, ADJ_STATUS_OK);
  /* One faulty frame between good ones never becomes a stored code. */
  publishTemperature(30, ADJ_STATUS_FUEL_TEMP_BROKEN);
  publishTemperature(30, ADJ_STATUS_OK);
  TEST_ASSERT_FALSE(fuelSensorCodeIs(DTC_KIND_STORED));

  publishTemperature(30, ADJ_STATUS_FUEL_TEMP_BROKEN);
  const uint32_t startedMs = hal_millis();
  while (!hal_elapsed_u32(hal_millis() + 5U, startedMs,
                          ECU_FUEL_TEMP_FAULT_CONFIRM_MS)) {
    publishTemperature(30, ADJ_STATUS_FUEL_TEMP_BROKEN);
    TEST_ASSERT_FALSE(fuelSensorCodeIs(DTC_KIND_ACTIVE));
  }
  publishTemperature(30, ADJ_STATUS_FUEL_TEMP_BROKEN);
  TEST_ASSERT_TRUE(fuelSensorCodeIs(DTC_KIND_ACTIVE));
  TEST_ASSERT_TRUE(fuelSensorCodeIs(DTC_KIND_STORED));

  publishTemperature(30, ADJ_STATUS_OK);
  TEST_ASSERT_FALSE(fuelSensorCodeIs(DTC_KIND_ACTIVE));
  TEST_ASSERT_TRUE(fuelSensorCodeIs(DTC_KIND_STORED));
}

static void test_lost_link_does_not_raise_fuel_sensor_code(void) {
  publishTemperature(30, ADJ_STATUS_OK);
  /* An intermittent link: core 0 first sees each good frame in a status
   * published after a later failed read. The sensor stays healthy. */
  const uint32_t goodFrames = (2U * ECU_FUEL_TEMP_FAULT_CONFIRM_MS) / 10U;
  const uint32_t readsBefore = pump.feedback.readCount;
  for (uint32_t i = 0U; i < goodFrames; i++) {
    processTemperature(30, ADJ_STATUS_OK, 2U);
    publishLostFrame();
    TEST_ASSERT_FALSE(isFuelTempValid());
  }
  TEST_ASSERT_EQUAL_UINT32(goodFrames, pump.feedback.readCount - readsBefore);
  /* Then a link lost for longer than the confirmation time. */
  for (uint32_t i = 0U; i < (2U * ECU_FUEL_TEMP_FAULT_CONFIRM_MS) / 5U; i++) {
    publishLostFrame();
  }
  TEST_ASSERT_FALSE(isFuelTempValid());
  TEST_ASSERT_FALSE(fuelSensorCodeIs(DTC_KIND_STORED));
}

static void test_cleared_fuel_sensor_code_returns_while_fault_lasts(void) {
  publishTemperature(30, ADJ_STATUS_FUEL_TEMP_BROKEN);
  for (uint32_t i = 0U; (i <= ECU_FUEL_TEMP_FAULT_CONFIRM_MS / 5U) &&
                        !fuelSensorCodeIs(DTC_KIND_ACTIVE);
       i++) {
    publishTemperature(30, ADJ_STATUS_FUEL_TEMP_BROKEN);
  }
  TEST_ASSERT_TRUE(fuelSensorCodeIs(DTC_KIND_ACTIVE));
  (void)dtcManagerClearAll();
  TEST_ASSERT_FALSE(fuelSensorCodeIs(DTC_KIND_ACTIVE));
  publishTemperature(30, ADJ_STATUS_FUEL_TEMP_BROKEN);
  TEST_ASSERT_TRUE(fuelSensorCodeIs(DTC_KIND_ACTIVE));
}

/* The next 0x12B frame of the periodic CAN0 group. */
static void sendEcuUpdate03(uint8_t *out) {
  if (canTestGetCanHandle() == NULL) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, canInit(1));
  }
  fiesta_test_can_reset(canTestGetCanHandle());
  CAN_updaterecipients_01();
  bool found = false;
  for (unsigned step = 0U; (step < 8U) && !found; step++) {
    fiesta_test_set_millis(hal_millis() + 2U);
    canMainLoop();
    uint32_t id = 0U;
    uint8_t length = 0U;
    uint8_t data[CAN_FRAME_MAX_LENGTH] = {0};
    while (fiesta_test_can_pop_sent(canTestGetCanHandle(), &id, &length,
                                    data) == HAL_OK) {
      if (id == CAN_ID_ECU_UPDATE_03) {
        TEST_ASSERT_EQUAL_UINT8(CAN_FRAME_MAX_LENGTH, length);
        memcpy(out, data, CAN_FRAME_MAX_LENGTH);
        found = true;
      }
    }
  }
  TEST_ASSERT_TRUE(found);
}

static void test_can_frame_carries_fuel_temperature_validity(void) {
  /* A fuel level with a non-zero high byte shares the buffer of 0x129. */
  setGlobalValue(F_FUEL, 4660.0f);
  uint8_t data[CAN_FRAME_MAX_LENGTH] = {0};
  publishTemperature(-20, ADJ_STATUS_OK);
  sendEcuUpdate03(data);
  TEST_ASSERT_EQUAL_HEX8(CAN_ECU_UPDATE_03_STATUS_FUEL_TEMP_VALID,
                         data[CAN_FRAME_ECU_UPDATE_03_STATUS]);
  TEST_ASSERT_EQUAL_INT8(-20, (int8_t)data[CAN_FRAME_ECU_UPDATE_FUEL_TEMP]);
  publishTemperature(25, ADJ_STATUS_FUEL_TEMP_BROKEN);
  sendEcuUpdate03(data);
  TEST_ASSERT_EQUAL_HEX8(0U, data[CAN_FRAME_ECU_UPDATE_03_STATUS]);
  TEST_ASSERT_EQUAL_HEX8(0U, data[CAN_FRAME_ECU_UPDATE_FUEL_TEMP]);
}

/* The reply to UDS 0x22 DD02 on the OBD-II bus. */
static void readFuelTemperatureDid(uint8_t *reply) {
  if (obdTestGetCanHandle() == NULL) {
    TEST_ASSERT_EQUAL_INT(HAL_OK, obdInit(1));
  }
  fiesta_test_can_reset(obdTestGetCanHandle());
  fiesta_test_can_set_state(obdTestGetCanHandle(), HAL_CAN_STATE_ERROR_ACTIVE);
  obdTestResetTransport();
  const uint8_t request[8] = {0x03U,
                              UDS_SVC_READ_DATA_BY_ID,
                              MSB(DID_FORD_FUEL_TEMP),
                              LSB(DID_FORD_FUEL_TEMP),
                              0U,
                              0U,
                              0U,
                              0U};
  obdReq(LISTEN_ID, request);
  uint32_t id = 0U;
  uint8_t length = 0U;
  TEST_ASSERT_EQUAL_INT(HAL_OK, fiesta_test_can_pop_sent(obdTestGetCanHandle(),
                                                         &id, &length, reply));
  TEST_ASSERT_EQUAL_UINT32(REPLY_ID, id);
}

static void test_obd_fuel_temperature_did_follows_validity(void) {
  uint8_t reply[8] = {0};
  publishTemperature(-20, ADJ_STATUS_OK);
  readFuelTemperatureDid(reply);
  TEST_ASSERT_EQUAL_HEX8(UDS_RSP_READ_DATA_BY_ID, reply[1]);
  TEST_ASSERT_EQUAL_HEX8(20U, reply[4]); /* -20 C + 40 */
  publishTemperature(25, ADJ_STATUS_FUEL_TEMP_BROKEN);
  readFuelTemperatureDid(reply);
  TEST_ASSERT_EQUAL_HEX8(UDS_RSP_NEGATIVE, reply[1]);
  TEST_ASSERT_EQUAL_HEX8(UDS_SVC_READ_DATA_BY_ID, reply[2]);
  TEST_ASSERT_EQUAL_HEX8(NRC_CONDITIONS_NOT_CORRECT, reply[3]);
}

#if VP37_TELEMETRY_ENABLED
static void test_native_adj_telemetry_prints_signed_celsius(void) {
  fiesta_test_set_millis(1000U);
  processTemperature(-20, ADJ_STATUS_OK, 2U);
  static VP37Snapshot snapshot;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readSnapshot(&pump, &snapshot));
  VP37_showDebug(&pump, &snapshot);
  const char *line = fiesta_test_debug_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "VP37 ADJ"));
  TEST_ASSERT_NOT_NULL(strstr(line, "ft:-20 "));
  TEST_ASSERT_NOT_NULL(strstr(line, "s:0 "));
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_calibrationTraceBegin());
  for (unsigned i = 0U; i < 3U; i++) {
    VP37_calibrationTraceRecord(&pump.adjustometer.last, 0U);
  }
  VP37_calibrationTraceEnd(&pump, true, true);
  TEST_ASSERT_EQUAL_INT(HAL_OK, hal_usb_init());
  fiesta_test_usb_connected(true);
  // The bounded drain prints two samples, leaving END for the next call.
  VP37_showCalibration();
  line = fiesta_test_debug_last_line();
  TEST_ASSERT_NOT_NULL(strstr(line, "VP37 CAL "));
  TEST_ASSERT_NOT_NULL(strstr(line, "ft:-20"));
  VP37CalibrationSample sample;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_readCalibrationTrace(&sample));
  VP37CalibrationResult result;
  TEST_ASSERT_EQUAL_INT(HAL_OK, VP37_takeCalibrationResult(&result));
}
#endif

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_cold_data_preserves_legacy_drive_policy);
  RUN_TEST(test_positive_readings_match_both_protocols);
  RUN_TEST(test_cold_and_invalid_hold_correction_but_report_distinct_status);
  RUN_TEST(test_ecu_globals_drop_invalid_temperature);
  RUN_TEST(test_fuel_sensor_code_confirms_after_window_and_clears);
  RUN_TEST(test_lost_link_does_not_raise_fuel_sensor_code);
  RUN_TEST(test_cleared_fuel_sensor_code_returns_while_fault_lasts);
  RUN_TEST(test_can_frame_carries_fuel_temperature_validity);
  RUN_TEST(test_obd_fuel_temperature_did_follows_validity);
#if VP37_TELEMETRY_ENABLED
  RUN_TEST(test_native_adj_telemetry_prints_signed_celsius);
#endif
  return UNITY_END();
}
