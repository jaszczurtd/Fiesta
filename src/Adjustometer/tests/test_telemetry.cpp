#include "hal/i2c/hal_i2c_slave.h"
#include "telemetry.h"
#include "utils/unity.h"
#include <cstring>

struct RegisterWrite {
  uint8_t reg, value;
};
static RegisterWrite writes[96];
static size_t writeCount;
static void recordWrite(uint8_t reg, uint8_t value) {
  TEST_ASSERT_LESS_THAN_UINT32(COUNTOF(writes), writeCount);
  writes[writeCount++] = {reg, value};
}

// Exercise the production publisher with individually scheduled register
// stores.
#define hal_i2c_slave_reg_write8 recordWrite
#include "../telemetry.c"
#undef hal_i2c_slave_reg_write8

void setUp(void) { writeCount = 0U; }
void tearDown(void) {}

static void applyWrite(uint8_t *map, const RegisterWrite &write) {
  map[write.reg] = write.value;
}

void test_reader_never_accepts_a_partially_published_feedback_frame(void) {
  adjustometer_feedback_t oldSample = {};
  oldSample.pulseHz = 8191;
  oldSample.rawHz = 31000;
  oldSample.filteredHz = 30000;
  oldSample.number = 25;
  oldSample.measuredUs = 100000;
  uint8_t oldMap[HAL_I2C_SLAVE_REG_MAP_SIZE] = {};
  publishAdjustometerFeedback(&oldSample);
  for (size_t i = 0; i < writeCount; i++) {
    applyWrite(oldMap, writes[i]);
  }
  adjustometer_feedback_t newSample = oldSample;
  newSample.pulseHz = 8192;
  newSample.rawHz = 28000;
  newSample.filteredHz = 29000;
  newSample.number++;
  newSample.measuredUs += 4000;
  writeCount = 0;
  publishAdjustometerFeedback(&newSample);
  uint8_t newMap[HAL_I2C_SLAVE_REG_MAP_SIZE];
  memcpy(newMap, oldMap, sizeof(newMap));
  for (size_t i = 0; i < writeCount; i++) {
    applyWrite(newMap, writes[i]);
  }
  for (size_t readPrefix = 0; readPrefix <= ADJUSTOMETER_FEEDBACK_BYTES;
       readPrefix++) {
    for (size_t writePrefix = 0; writePrefix <= writeCount; writePrefix++) {
      uint8_t map[HAL_I2C_SLAVE_REG_MAP_SIZE];
      uint8_t frame[ADJUSTOMETER_FEEDBACK_BYTES];
      memcpy(map, oldMap, sizeof(map));
      memcpy(frame, map + ADJUSTOMETER_FEEDBACK_START, readPrefix);
      for (size_t i = 0; i < writePrefix; i++) {
        applyWrite(map, writes[i]);
      }
      memcpy(frame + readPrefix, map + ADJUSTOMETER_FEEDBACK_START + readPrefix,
             COUNTOF(frame) - readPrefix);
      adjustometer_feedback_t decoded;
      if (adjustometer_feedback_decode(frame, &decoded) == HAL_OK) {
        const bool isOld =
            memcmp(frame + 2, oldMap + ADJUSTOMETER_FEEDBACK_START + 2,
                   ADJUSTOMETER_FEEDBACK_BYTES - 3U) == 0;
        const bool isNew =
            memcmp(frame + 2, newMap + ADJUSTOMETER_FEEDBACK_START + 2,
                   ADJUSTOMETER_FEEDBACK_BYTES - 3U) == 0;
        TEST_ASSERT_TRUE(isOld || isNew);
      }
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_reader_never_accepts_a_partially_published_feedback_frame);
  return UNITY_END();
}
