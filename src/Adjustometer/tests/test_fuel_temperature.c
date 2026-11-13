#include "../../common/tests/hal_mock_c_bridge.h"
#include "sensors.h"
#include "unity.h"
#include <string.h>

void setUp(void) {
  fiesta_test_set_micros(1U);
  fiesta_test_adc_inject(JH_HW_NODE_SUPPLY_VOLTAGE_PIN_IN, 2600);
  initSensors();
}

void tearDown(void) {}

static adjustometer_feedback_t sampleAdc(int adc) {
  fiesta_test_adc_inject(JH_HW_NODE_FUEL_TEMPERATURE_PIN_IN, adc);
  updateAuxiliarySensors();
  adjustometer_feedback_t sample = {0};
  TEST_ASSERT_EQUAL_INT(HAL_OK, getAdjustometerFeedback(&sample));
  return sample;
}

static void test_adc_to_frame_preserves_negative_and_zero(void) {
  /* Fixed ADC codes for the configured NTC divider, including RP2040 DNL.
   * Values exercise the real ADC averaging, NTC conversion and producer. */
  const int adc[] = {3836, 3395, 2601, 2563, 1012, 128};
  const int16_t expected[] = {-40, -20, 0, 1, 40, 120};
  const uint8_t bytes[] = {0, 20, 40, 41, 80, 160};
  for (size_t i = 0U; i < COUNTOF(adc); i++) {
    initSensors();
    const adjustometer_feedback_t sample = sampleAdc(adc[i]);
    TEST_ASSERT_EQUAL_INT16(expected[i], sample.fuelTemp);
    TEST_ASSERT_BITS_LOW(ADJ_STATUS_FUEL_TEMP_BROKEN, sample.status);
    uint8_t frame[ADJUSTOMETER_FEEDBACK_BYTES];
    adjustometer_feedback_encode(frame, &sample, 2U);
    TEST_ASSERT_EQUAL_UINT8(2U, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(bytes[i], frame[5]);
    adjustometer_feedback_t decoded = {0};
    TEST_ASSERT_EQUAL_INT(HAL_OK,
                          adjustometer_feedback_decode(frame, &decoded));
    TEST_ASSERT_EQUAL_INT16(expected[i], decoded.fuelTemp);
  }
}

static void test_negative_ema_and_zero_crossing(void) {
  adjustometer_feedback_t sample = sampleAdc(3395); /* -20 C */
  TEST_ASSERT_EQUAL_INT16(-20, sample.fuelTemp);
  sample = sampleAdc(3113); /* -12 C: one 1/8 EMA step, not reinitialization. */
  TEST_ASSERT_EQUAL_INT16(-19, sample.fuelTemp);
  sample = sampleAdc(1012); /* +40 C: still below zero after one step. */
  TEST_ASSERT_EQUAL_INT16(-12, sample.fuelTemp);
  TEST_ASSERT_BITS_LOW(ADJ_STATUS_FUEL_TEMP_BROKEN, sample.status);
  for (unsigned i = 0U; i < 60U; i++) {
    sample = sampleAdc(1012);
  }
  TEST_ASSERT_EQUAL_INT16(40, sample.fuelTemp);
}

static void test_fault_is_immediate_and_recovery_reseeds_filter(void) {
  const int badAdc[] = {4095, 0, -1, 3851, 124};
  for (size_t i = 0U; i < COUNTOF(badAdc); i++) {
    initSensors();
    adjustometer_feedback_t sample = sampleAdc(1012);
    TEST_ASSERT_EQUAL_INT16(40, sample.fuelTemp);
    sample = sampleAdc(badAdc[i]);
    TEST_ASSERT_BITS_HIGH(ADJ_STATUS_FUEL_TEMP_BROKEN, sample.status);
    TEST_ASSERT_EQUAL_INT16(40,
                            sample.fuelTemp); /* Last good value, invalid. */
    sample = sampleAdc(3395);
    TEST_ASSERT_BITS_LOW(ADJ_STATUS_FUEL_TEMP_BROKEN, sample.status);
    TEST_ASSERT_EQUAL_INT16(-20, sample.fuelTemp);
  }
}

static void test_startup_and_failed_read_do_not_claim_zero(void) {
  adjustometer_feedback_t sample = {0};
  TEST_ASSERT_EQUAL_INT(HAL_OK, getAdjustometerFeedback(&sample));
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_FUEL_TEMP_BROKEN, sample.status);
  fiesta_test_adc_inject(JH_HW_NODE_FUEL_TEMPERATURE_PIN_IN, 4095);
  int16_t temperature = 77;
  TEST_ASSERT_EQUAL_INT(HAL_EIO, getFuelTemperatureRaw(&temperature));
  TEST_ASSERT_EQUAL_INT16(77, temperature);
  TEST_ASSERT_EQUAL_INT(HAL_EINVAL, getFuelTemperatureRaw(NULL));
  sample = sampleAdc(2601);
  TEST_ASSERT_EQUAL_INT16(0, sample.fuelTemp);
  TEST_ASSERT_BITS_LOW(ADJ_STATUS_FUEL_TEMP_BROKEN, sample.status);
}

static void test_protocol_versions_and_errors(void) {
  uint8_t frame[ADJUSTOMETER_FEEDBACK_BYTES] = {0};
  frame[0] = 1U;
  frame[5] = 40U;
  adjustometer_feedback_t sample = {0};
  TEST_ASSERT_EQUAL_INT(HAL_OK, adjustometer_feedback_decode(frame, &sample));
  TEST_ASSERT_EQUAL_INT16(40, sample.fuelTemp);
  frame[0] = 2U;
  TEST_ASSERT_EQUAL_INT(HAL_OK, adjustometer_feedback_decode(frame, &sample));
  TEST_ASSERT_EQUAL_INT16(0, sample.fuelTemp);
  frame[5] = 0U;
  TEST_ASSERT_EQUAL_INT(HAL_OK, adjustometer_feedback_decode(frame, &sample));
  TEST_ASSERT_EQUAL_INT16(-40, sample.fuelTemp);
  frame[6] = ADJ_STATUS_FUEL_TEMP_BROKEN;
  TEST_ASSERT_EQUAL_INT(HAL_OK, adjustometer_feedback_decode(frame, &sample));
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_FUEL_TEMP_BROKEN, sample.status);
  /* Poison the whole destination: a successful struct assignment above may
   * leave padding undefined, which is not part of a decoded reading. */
  memset(&sample, 0x5a, sizeof(sample));
  uint8_t before[sizeof(sample)];
  memcpy(before, &sample, sizeof(sample));
  frame[0] = 3U;
  TEST_ASSERT_EQUAL_INT(HAL_EUNSUPPORTED,
                        adjustometer_feedback_decode(frame, &sample));
  TEST_ASSERT_EQUAL_MEMORY(&before, &sample, sizeof(sample));
  frame[0] = 2U;
  frame[1] = 1U;
  TEST_ASSERT_EQUAL_INT(HAL_EAGAIN,
                        adjustometer_feedback_decode(frame, &sample));
  TEST_ASSERT_EQUAL_MEMORY(&before, &sample, sizeof(sample));
  sample.fuelTemp = -41;
  sample.status = ADJ_STATUS_OK;
  adjustometer_feedback_encode(frame, &sample, 2U);
  TEST_ASSERT_BITS_HIGH(ADJ_STATUS_FUEL_TEMP_BROKEN, frame[6]);
  TEST_ASSERT_EQUAL_UINT8(40U, frame[5]); /* No signed wrapping. */
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_adc_to_frame_preserves_negative_and_zero);
  RUN_TEST(test_negative_ema_and_zero_crossing);
  RUN_TEST(test_fault_is_immediate_and_recovery_reseeds_filter);
  RUN_TEST(test_startup_and_failed_read_do_not_claim_zero);
  RUN_TEST(test_protocol_versions_and_errors);
  return UNITY_END();
}
