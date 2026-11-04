#include "unity.h"
#include "vp37_module_fixture.h"

// ── Helpers ──────────────────────────────────────────────────────────────────

static VP37Pump s_pump;

static adjustometer_reading_t readAdjustometer(void) {
  return VP37_readAdjustometer(&s_pump);
}

static void injectAdjExtendedData(uint8_t version, uint8_t seqBegin,
                                  uint8_t seqEnd, uint8_t flags,
                                  uint32_t signalHz, uint32_t baselineHz,
                                  int16_t chipTempDeciC) {
  uint8_t buf[ADJUSTOMETER_EXT_REG_COUNT] = {0};
  buf[ADJUSTOMETER_REG_EXT_VERSION - ADJUSTOMETER_EXT_REG_START] = version;
  buf[ADJUSTOMETER_REG_EXT_SEQ_BEGIN - ADJUSTOMETER_EXT_REG_START] = seqBegin;
  buf[ADJUSTOMETER_REG_EXT_FLAGS - ADJUSTOMETER_EXT_REG_START] = flags;
  jh_store_be32(buf + ADJUSTOMETER_REG_SIGNAL_HZ - ADJUSTOMETER_EXT_REG_START,
                signalHz);
  jh_store_be32(buf + ADJUSTOMETER_REG_BASELINE_HZ - ADJUSTOMETER_EXT_REG_START,
                baselineHz);
  jh_store_be16(buf + ADJUSTOMETER_REG_CHIP_TEMP_DECI_C -
                    ADJUSTOMETER_EXT_REG_START,
                (uint16_t)chipTempDeciC);
  buf[ADJUSTOMETER_REG_EXT_SEQ_END - ADJUSTOMETER_EXT_REG_START] = seqEnd;
  injectAdjustometerBytes(buf, ADJUSTOMETER_EXT_REG_COUNT);
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void setUp(void) {
  setUpVp37Fixture();
  hal_mock_adc_inject(JH_HW_NODE_VP37_PIN_SUPPLY_ADC, 0);
  memset(&s_pump, 0, sizeof(s_pump));
  (void)VP37_setCallbacks(&s_pump, vp37ModuleCallbacks());
}

void tearDown(void) { vp37AdjustometerTransferBusy(false); }

// ── Adjustometer readout tests ───────────────────────────────────────────────

void test_adjustometer_positive_pulse(void) {
  // +500 Hz deviation, 13.2 V, 45 °C, status OK
  injectAdjRegisterData(500, 132, 45, ADJ_STATUS_OK);
  adjustometer_reading_t r = readAdjustometer();
  TEST_ASSERT_EQUAL_INT32(500, r.pulseHz);
}

void test_adjustometer_negative_pulse(void) {
  // -200 Hz deviation
  injectAdjRegisterData(-200, 120, 30, ADJ_STATUS_OK);
  adjustometer_reading_t r = readAdjustometer();
  TEST_ASSERT_EQUAL_INT32(-200, r.pulseHz);
}

void test_adjustometer_zero_pulse(void) {
  injectAdjRegisterData(0, 140, 50, ADJ_STATUS_OK);
  adjustometer_reading_t r = readAdjustometer();
  TEST_ASSERT_EQUAL_INT32(0, r.pulseHz);
}

void test_adjustometer_large_negative(void) {
  // INT16_MIN boundary
  injectAdjRegisterData(-32768, 100, 20, ADJ_STATUS_OK);
  adjustometer_reading_t r = readAdjustometer();
  TEST_ASSERT_EQUAL_INT32(-32768, r.pulseHz);
}

static float adjustometerSupplyVolts(void) {
  TEST_ASSERT_TRUE(VP37_updateAdjustometerPosition(&s_pump));
  return s_pump.feedback.supplyVolts;
}

void test_adjustometer_voltage_conversion(void) {
  // Register value 138 -> 13.8 V in the pump; the ECU reads its own divider.
  injectAdjRegisterData(0, 138, 40, ADJ_STATUS_OK);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 13.8f, adjustometerSupplyVolts());
}

void test_adjustometer_voltage_zero(void) {
  injectAdjRegisterData(0, 0, 25, ADJ_STATUS_OK);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, adjustometerSupplyVolts());
}

void test_adjustometer_voltage_max(void) {
  // 255 -> 25.5 V
  injectAdjRegisterData(0, 255, 20, ADJ_STATUS_OK);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 25.5f, adjustometerSupplyVolts());
}

void test_adjustometer_fuel_temp(void) {
  injectAdjRegisterData(0, 130, 85, ADJ_STATUS_OK);
  adjustometer_reading_t r = readAdjustometer();
  TEST_ASSERT_EQUAL_UINT8(85, r.fuelTempC);
}

void test_adjustometer_fuel_temp_zero(void) {
  injectAdjRegisterData(0, 130, 0, ADJ_STATUS_OK);
  adjustometer_reading_t r = readAdjustometer();
  TEST_ASSERT_EQUAL_UINT8(0, r.fuelTempC);
}

void test_adjustometer_feedback_does_not_require_extension(void) {
  injectAdjRegisterData(321, 137, 44, ADJ_STATUS_OK);
  adjustometer_reading_t r = readAdjustometer();

  TEST_ASSERT_TRUE(r.commOk);
  TEST_ASSERT_EQUAL_INT32(321, r.pulseHz);
  TEST_ASSERT_FALSE(r.extendedTelemetryValid);
}

void test_adjustometer_extended_telemetry_decodes_coherent_snapshot(void) {
  injectAdjRegisterData(500, 132, 45, ADJ_STATUS_OK);
  adjustometer_reading_t r = readAdjustometer();

  const uint8_t flags = ADJUSTOMETER_EXT_FLAG_SIGNAL_VALID |
                        ADJUSTOMETER_EXT_FLAG_BASELINE_VALID |
                        ADJUSTOMETER_EXT_FLAG_CHIP_TEMP_VALID;
  injectAdjExtendedData(ADJUSTOMETER_EXT_VERSION, 12U, 12U, flags, 3654321U,
                        3620000U, 427);

  TEST_ASSERT_TRUE(VP37_readAdjustometerExtended(&s_pump, &r));
  TEST_ASSERT_TRUE(r.commOk);
  TEST_ASSERT_TRUE(r.extendedTelemetryValid);
  TEST_ASSERT_EQUAL_UINT32(3654321U, r.signalHz);
  TEST_ASSERT_EQUAL_UINT32(3620000U, r.baselineHz);
  TEST_ASSERT_EQUAL_INT32(34321, r.signedDeltaHz);
  TEST_ASSERT_EQUAL_INT16(427, r.chipTempDeciC);
  TEST_ASSERT_EQUAL_UINT8(flags, r.extendedFlags);
  injectAdjExtendedData(ADJUSTOMETER_EXT_VERSION, 14U, 14U, flags, 3620000U,
                        3654321U, -125);
  TEST_ASSERT_TRUE(VP37_readAdjustometerExtended(&s_pump, &r));
  TEST_ASSERT_EQUAL_INT32(-34321, r.signedDeltaHz);
  TEST_ASSERT_EQUAL_INT16(-125, r.chipTempDeciC);
  injectAdjExtendedData(ADJUSTOMETER_EXT_VERSION, 16U, 16U, flags, 3620000U,
                        3620000U, 0);
  TEST_ASSERT_TRUE(VP37_readAdjustometerExtended(&s_pump, &r));
  TEST_ASSERT_EQUAL_INT32(0, r.signedDeltaHz);
}

void test_adjustometer_extended_telemetry_rejects_unknown_version(void) {
  adjustometer_reading_t r = {};
  r.signedDeltaHz = 123;
  injectAdjExtendedData(1U, 2U, 2U, 0U, 36000U, 35000U, 250);

  TEST_ASSERT_FALSE(VP37_readAdjustometerExtended(&s_pump, &r));
  TEST_ASSERT_EQUAL_INT32(123, r.signedDeltaHz);
}

void test_adjustometer_extended_telemetry_rejects_torn_snapshot(void) {
  adjustometer_reading_t r = {};
  injectAdjExtendedData(ADJUSTOMETER_EXT_VERSION, 4U, 6U, 0U, 36000U, 35000U,
                        250);

  TEST_ASSERT_FALSE(VP37_readAdjustometerExtended(&s_pump, &r));
}

// ── Adjustometer status flags are currently informational only ──────────────

void test_adjustometer_status_signal_lost_is_reported_not_acted_on(void) {
  injectAdjRegisterData(0, 120, 40, ADJ_STATUS_SIGNAL_LOST);
  adjustometer_reading_t r = readAdjustometer();
  TEST_ASSERT_EQUAL_INT32(0, r.pulseHz);
}

void test_adjustometer_status_fuel_temp_broken_is_reported_not_acted_on(void) {
  injectAdjRegisterData(0, 120, 0, ADJ_STATUS_FUEL_TEMP_BROKEN);
  adjustometer_reading_t r = readAdjustometer();
  TEST_ASSERT_EQUAL_UINT8(ADJ_STATUS_FUEL_TEMP_BROKEN, r.status);
}

void test_adjustometer_status_voltage_bad_is_reported_not_acted_on(void) {
  injectAdjRegisterData(0, 50, 40, ADJ_STATUS_VOLTAGE_BAD);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 5.0f, adjustometerSupplyVolts());
  TEST_ASSERT_EQUAL_UINT8(ADJ_STATUS_VOLTAGE_BAD, s_pump.feedback.lastStatus);
}

void test_adjustometer_comm_lost_on_nack(void) {
  // A failed transfer invalidates the reading but retains its last values.
  injectAdjRegisterData(700, 138, 40, ADJ_STATUS_OK);
  adjustometer_reading_t r = readAdjustometer();
  TEST_ASSERT_TRUE(r.commOk);
  vp37AdjustometerTransferBusy(true);
  r = readAdjustometer();
  TEST_ASSERT_FALSE(r.commOk);
  TEST_ASSERT_EQUAL_INT32(700, r.pulseHz);
  vp37AdjustometerTransferBusy(false);
  injectAdjRegisterData(710, 138, 40, ADJ_STATUS_OK);
  TEST_ASSERT_TRUE(readAdjustometer().commOk);
}

void test_adjustometer_rejects_removed_legacy_frame(void) {
  const uint8_t legacy[5] = {0x02, 0xbc, 138, 40, 0};
  injectAdjustometerBytes(legacy, (int)COUNTOF(legacy));
  const adjustometer_reading_t r = readAdjustometer();
  TEST_ASSERT_FALSE(r.commOk);
  TEST_ASSERT_FALSE(r.feedbackFresh);
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED, r.readStatus);
  TEST_ASSERT_EQUAL_INT32(0, r.pulseHz);
}

// ── Int16 BE encoding edge cases ────────────────────────────────────────────

void test_adjustometer_big_endian_byte_order(void) {
  // Manually inject raw bytes: 0x01, 0x00 -> +256 Hz
  uint8_t buf[30] = {1, 2, 0x01, 0x00, 130, 50, 0x00};
  buf[29] = 2;
  injectAdjustometerBytes(buf, (int)COUNTOF(buf));
  adjustometer_reading_t r = readAdjustometer();
  TEST_ASSERT_EQUAL_INT32(256, r.pulseHz);
}

void test_adjustometer_big_endian_negative(void) {
  // 0xFF, 0x00 -> -256 in int16_t (0xFF00 = -256)
  uint8_t buf[30] = {1, 2, 0xFF, 0x00, 130, 50, 0x00};
  buf[29] = 2;
  injectAdjustometerBytes(buf, (int)COUNTOF(buf));
  adjustometer_reading_t r = readAdjustometer();
  TEST_ASSERT_EQUAL_INT32(-256, r.pulseHz);
}

// ── Runner ───────────────────────────────────────────────────────────────────

void test_fast_feedback_age_sequence_and_clock_wrap(void) {
  adjustometer_feedback_t sample = {};
  sample.number = 42U;
  sample.measuredUs = UINT32_MAX - 100U;
  sample.rawHz = 26000U;
  sample.filteredHz = 26500U;
  sample.baselineHz = 34000U;
  sample.pulseHz = 7500;
  sample.ageUs = 700U;
  hal_mock_set_micros(100000);
  injectFastAdjustometer(sample);
  adjustometer_reading_t reading = readAdjustometer();
  TEST_ASSERT_TRUE(reading.commOk && reading.feedbackFresh);
  TEST_ASSERT_EQUAL_UINT32(26000U, reading.rawHz);
  TEST_ASSERT_EQUAL_INT16(7500, reading.pulseHz);

  hal_mock_set_micros(105000);
  injectFastAdjustometer(sample);
  reading = readAdjustometer();
  TEST_ASSERT_TRUE(reading.feedbackFresh);
  hal_mock_set_micros(120000);
  injectFastAdjustometer(sample);
  reading = readAdjustometer();
  TEST_ASSERT_FALSE(reading.feedbackFresh);

  sample.number++;
  sample.measuredUs = 200U; // Natural clock wrap remains forward progress.
  injectFastAdjustometer(sample);
  reading = readAdjustometer();
  TEST_ASSERT_TRUE(reading.feedbackFresh);
  sample.number++;
  sample.measuredUs =
      100U; // A sensor restart cannot retain the ECU calibration.
  injectFastAdjustometer(sample);
  reading = readAdjustometer();
  TEST_ASSERT_FALSE(reading.feedbackFresh);
  sample.number++;
  sample.measuredUs = 400U;
  sample.ageUs = ADJUSTOMETER_FEEDBACK_MAX_AGE_US + 1U;
  injectFastAdjustometer(sample);
  reading = readAdjustometer();
  TEST_ASSERT_FALSE(reading.feedbackFresh);
}

void test_fast_feedback_rejects_mixed_or_unknown_frames(void) {
  adjustometer_feedback_t sample = {};
  uint8_t frames[ADJUSTOMETER_FEEDBACK_BYTES * 2U];
  adjustometer_feedback_encode(frames, &sample, 4U);
  frames[ADJUSTOMETER_FEEDBACK_BYTES - 1U] = 5U;
  memcpy(frames + ADJUSTOMETER_FEEDBACK_BYTES, frames,
         ADJUSTOMETER_FEEDBACK_BYTES);
  injectAdjustometerBytes(frames, (int)COUNTOF(frames));
  adjustometer_reading_t reading = readAdjustometer();
  TEST_ASSERT_FALSE(reading.commOk);
  frames[0] = 99U;
  injectAdjustometerBytes(frames, ADJUSTOMETER_FEEDBACK_BYTES);
  reading = readAdjustometer();
  TEST_ASSERT_FALSE(reading.commOk);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_fast_feedback_age_sequence_and_clock_wrap);
  RUN_TEST(test_fast_feedback_rejects_mixed_or_unknown_frames);

  // Adjustometer readout
  RUN_TEST(test_adjustometer_positive_pulse);
  RUN_TEST(test_adjustometer_negative_pulse);
  RUN_TEST(test_adjustometer_zero_pulse);
  RUN_TEST(test_adjustometer_large_negative);
  RUN_TEST(test_adjustometer_voltage_conversion);
  RUN_TEST(test_adjustometer_voltage_zero);
  RUN_TEST(test_adjustometer_voltage_max);
  RUN_TEST(test_adjustometer_fuel_temp);
  RUN_TEST(test_adjustometer_fuel_temp_zero);
  RUN_TEST(test_adjustometer_feedback_does_not_require_extension);
  RUN_TEST(test_adjustometer_extended_telemetry_decodes_coherent_snapshot);
  RUN_TEST(test_adjustometer_extended_telemetry_rejects_unknown_version);
  RUN_TEST(test_adjustometer_extended_telemetry_rejects_torn_snapshot);

  // Byte order
  RUN_TEST(test_adjustometer_big_endian_byte_order);
  RUN_TEST(test_adjustometer_big_endian_negative);

  // DTC status
  RUN_TEST(test_adjustometer_status_signal_lost_is_reported_not_acted_on);
  RUN_TEST(test_adjustometer_status_fuel_temp_broken_is_reported_not_acted_on);
  RUN_TEST(test_adjustometer_status_voltage_bad_is_reported_not_acted_on);
  RUN_TEST(test_adjustometer_comm_lost_on_nack);
  RUN_TEST(test_adjustometer_rejects_removed_legacy_frame);

  return UNITY_END();
}
