#include "telemetry.h"
#include <hal/i2c/hal_i2c_slave.h>

#if defined(__cplusplus)
static_assert(ADJUSTOMETER_FEEDBACK_START + ADJUSTOMETER_FEEDBACK_BYTES <=
                  HAL_I2C_SLAVE_REG_MAP_SIZE,
              "Adjustometer feedback exceeds register map");
#else
_Static_assert(ADJUSTOMETER_FEEDBACK_START + ADJUSTOMETER_FEEDBACK_BYTES <=
                   HAL_I2C_SLAVE_REG_MAP_SIZE,
               "Adjustometer feedback exceeds register map");
#endif

/* Each block has one writer; the HAL freezes complete publications for I2C. */
static void publishSequencedRegisters(uint8_t start, uint8_t *frame,
                                      uint8_t count, uint8_t *sequence) {
  const uint8_t next = (uint8_t)((*sequence + 2U) & 0xFEU);
  frame[1] = next;
  frame[count - 1U] = next;
  const hal_status_t status =
      hal_i2c_slave_reg_write_block(start, frame, count);
  HAL_ASSERT(status == HAL_OK, "Adjustometer publication exceeds register map");
  if (status == HAL_OK) {
    *sequence = next;
  }
}

void publishAdjustometerFeedback(const adjustometer_feedback_t *sample) {
  static uint8_t sequence = 0U;
  uint8_t frame[ADJUSTOMETER_FEEDBACK_BYTES];
  adjustometer_feedback_encode(frame, sample, 0U);
  publishSequencedRegisters(ADJUSTOMETER_FEEDBACK_START, frame,
                            (uint8_t)COUNTOF(frame), &sequence);
  hal_i2c_slave_reg_write16(ADJUSTOMETER_REG_PULSE_HI,
                            (uint16_t)sample->pulseHz);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_VOLTAGE, sample->voltage);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_FUEL_TEMP, sample->fuelTemp);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_STATUS, sample->status);
}

void publishAdjustometerExtension(const adjustometer_feedback_t *sample,
                                  int16_t chipTemp) {
  static uint8_t sequence = 0U;
  uint8_t frame[ADJUSTOMETER_EXT_REG_COUNT] = {0};
  frame[0] = ADJUSTOMETER_EXT_VERSION;
  if ((sample->status & ADJ_STATUS_SIGNAL_LOST) == 0U) {
    frame[2] |= ADJUSTOMETER_EXT_FLAG_SIGNAL_VALID;
  }
  if ((sample->status & ADJ_STATUS_BASELINE_PENDING) == 0U) {
    frame[2] |= ADJUSTOMETER_EXT_FLAG_BASELINE_VALID;
  }
  if (chipTemp != INT16_MIN) {
    frame[2] |= ADJUSTOMETER_EXT_FLAG_CHIP_TEMP_VALID;
  }
  jh_store_be32(frame + 3, sample->filteredHz);
  jh_store_be32(frame + 7, sample->baselineHz);
  jh_store_be32(frame + 11, (uint32_t)((int32_t)sample->filteredHz -
                                       (int32_t)sample->baselineHz));
  jh_store_be16(frame + 15, (uint16_t)chipTemp);
  publishSequencedRegisters(ADJUSTOMETER_EXT_REG_START, frame,
                            (uint8_t)COUNTOF(frame), &sequence);
}
