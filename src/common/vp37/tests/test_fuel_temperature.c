#include "../../tests/hal_mock_c_bridge.h"
#include "sensors.h"
#include "unity.h"
#include "vp37_adapter.h"
#include "vp37_pump_setup.h"
#include <hal/usb/hal_usb.h>
#include <math.h>
#include <string.h>

static VP37Pump pump;
static uint8_t frame[ADJUSTOMETER_FEEDBACK_BYTES];
static int32_t writtenPwm;

static void writePwm(int32_t value) { writtenPwm = value; }
static void enableDrive(bool enabled) { (void)enabled; }
static bool driveEnabled(void) { return true; }
static hal_status_t transfer(uint8_t reg, uint8_t *out, size_t length) {
  if ((reg != ADJUSTOMETER_FEEDBACK_START) || (length != COUNTOF(frame))) {
    return HAL_EBUS;
  }
  memcpy(out, frame, length);
  return HAL_OK;
}

void setUp(void) {
  fiesta_test_set_millis(0U);
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
  }
  processTemperature(49, ADJ_STATUS_OK, 2U);
  TEST_ASSERT_GREATER_THAN_FLOAT(factor, pump.thermal.temperatureCorrection);
  TEST_ASSERT_LESS_THAN_FLOAT(1.0f, pump.thermal.temperatureCorrection);
  TEST_ASSERT_GREATER_THAN_FLOAT(VP37_PID_CORR_LIMIT_POSITIVE_COLD,
                                 pump.pid.positiveLimit);
}

static void test_ecu_globals_keep_last_valid_temperature(void) {
  pump.feedback.readCount = 1000U;
  processTemperature(-20, ADJ_STATUS_OK, 2U);
  vp37AdapterPublish(&pump);
  TEST_ASSERT_EQUAL_FLOAT(-20.0f, getGlobalValue(F_FUEL_TEMP));
  processTemperature(0, ADJ_STATUS_FUEL_TEMP_BROKEN, 2U);
  vp37AdapterPublish(&pump);
  TEST_ASSERT_EQUAL_FLOAT(-20.0f, getGlobalValue(F_FUEL_TEMP));
  processTemperature(0, ADJ_STATUS_OK, 2U);
  vp37AdapterPublish(&pump);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, getGlobalValue(F_FUEL_TEMP));
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
  RUN_TEST(test_ecu_globals_keep_last_valid_temperature);
#if VP37_TELEMETRY_ENABLED
  RUN_TEST(test_native_adj_telemetry_prints_signed_celsius);
#endif
  return UNITY_END();
}
