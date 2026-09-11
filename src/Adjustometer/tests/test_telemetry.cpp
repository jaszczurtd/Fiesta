#include "hal/i2c/hal_i2c_slave.h"
#include "hal/impl/.mock/hal_mock.h"
#include "telemetry.h"
#include "utils/unity.h"
#include <cstring>

#include "../telemetry.c"

static adjustometer_feedback_t nextSample;
static uint8_t updateAfter;

void setUp(void) {
  hal_mock_i2c_slave_set_read_hook(nullptr);
  hal_i2c_slave_init(4, 5, 0x57);
}
void tearDown(void) { hal_mock_i2c_slave_set_read_hook(nullptr); }

static adjustometer_feedback_t makeSample(uint32_t number) {
  adjustometer_feedback_t sample = {};
  sample.pulseHz = (int16_t)(8100U + number);
  sample.rawHz = 31000U + number;
  sample.filteredHz = 30000U + number;
  sample.baselineHz = 29000U + number;
  sample.number = number;
  sample.measuredUs = 100000U + number * 1000U;
  sample.ageUs = 150U;
  sample.voltage = 140U;
  sample.fuelTemp = 28U;
  return sample;
}

static void publishDuringRead(uint8_t bus, uint8_t reg) {
  TEST_ASSERT_EQUAL_UINT8(0U, bus);
  if (reg == updateAfter) {
    publishAdjustometerFeedback(&nextSample);
    publishAdjustometerExtension(&nextSample, 350);
  }
}

static void readFeedback(uint8_t *frame) {
  const uint8_t start = ADJUSTOMETER_FEEDBACK_START;
  hal_mock_i2c_slave_simulate_receive(&start, 1);
  TEST_ASSERT_EQUAL_INT(
      ADJUSTOMETER_FEEDBACK_BYTES,
      hal_mock_i2c_slave_simulate_request(frame, ADJUSTOMETER_FEEDBACK_BYTES));
}

static void assertSample(const adjustometer_feedback_t *sample,
                         const uint8_t *frame) {
  adjustometer_feedback_t decoded;
  TEST_ASSERT_EQUAL_INT(HAL_OK, adjustometer_feedback_decode(frame, &decoded));
  uint8_t expected[ADJUSTOMETER_FEEDBACK_BYTES];
  adjustometer_feedback_encode(expected, sample, frame[1]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, COUNTOF(expected));
}

void test_reader_gets_one_publication_at_every_byte_boundary(void) {
  const adjustometer_feedback_t oldSample = makeSample(25U);
  nextSample = makeSample(26U);
  for (uint8_t split = 0U; split < ADJUSTOMETER_FEEDBACK_BYTES; ++split) {
    publishAdjustometerFeedback(&oldSample);
    updateAfter = (uint8_t)(ADJUSTOMETER_FEEDBACK_START + split);
    hal_mock_i2c_slave_set_read_hook(publishDuringRead);
    uint8_t frame[ADJUSTOMETER_FEEDBACK_BYTES];
    readFeedback(frame);
    assertSample(&oldSample, frame);
    hal_mock_i2c_slave_set_read_hook(nullptr);
    readFeedback(frame);
    assertSample(&nextSample, frame);
  }
}

void test_complete_frames_remain_valid_across_sequence_wrap(void) {
  uint8_t previous = 0U;
  for (uint32_t number = 0U; number < 140U; ++number) {
    const adjustometer_feedback_t sample = makeSample(number);
    publishAdjustometerFeedback(&sample);
    uint8_t frame[ADJUSTOMETER_FEEDBACK_BYTES];
    readFeedback(frame);
    assertSample(&sample, frame);
    if (number != 0U) {
      TEST_ASSERT_EQUAL_UINT8((uint8_t)(previous + 2U), frame[1]);
    }
    previous = frame[1];
  }
}

void test_publisher_keeps_legacy_and_extension_registers(void) {
  const adjustometer_feedback_t sample = makeSample(25U);
  publishAdjustometerFeedback(&sample);
  publishAdjustometerExtension(&sample, 350);
  TEST_ASSERT_EQUAL_UINT16((uint16_t)sample.pulseHz,
                           hal_i2c_slave_reg_read16(ADJUSTOMETER_REG_PULSE_HI));
  TEST_ASSERT_EQUAL_UINT8(sample.voltage,
                          hal_i2c_slave_reg_read8(ADJUSTOMETER_REG_VOLTAGE));
  const uint8_t start = ADJUSTOMETER_EXT_REG_START;
  uint8_t frame[ADJUSTOMETER_EXT_REG_COUNT];
  hal_mock_i2c_slave_simulate_receive(&start, 1);
  TEST_ASSERT_EQUAL_INT(COUNTOF(frame), hal_mock_i2c_slave_simulate_request(
                                            frame, COUNTOF(frame)));
  TEST_ASSERT_EQUAL_UINT8(ADJUSTOMETER_EXT_VERSION, frame[0]);
  TEST_ASSERT_EQUAL_UINT8(0U, frame[1] & 1U);
  TEST_ASSERT_EQUAL_UINT8(frame[1], frame[COUNTOF(frame) - 1U]);
  TEST_ASSERT_EQUAL_UINT32(sample.filteredHz, jh_load_be32(frame + 3));
  TEST_ASSERT_EQUAL_UINT32(sample.baselineHz, jh_load_be32(frame + 7));
  TEST_ASSERT_EQUAL_UINT16(350U, jh_load_be16(frame + 15));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_reader_gets_one_publication_at_every_byte_boundary);
  RUN_TEST(test_complete_frames_remain_valid_across_sequence_wrap);
  RUN_TEST(test_publisher_keeps_legacy_and_extension_registers);
  return UNITY_END();
}
