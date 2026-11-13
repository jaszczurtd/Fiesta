#pragma once

#include <stdint.h>

/*
 * Adjustometer I2C register map.
 *
 * Registers 0x05..0x12 provide optional diagnostics. The coherent feedback
 * block stays at 0x17 for control. Registers 0x00..0x04 and 0x13..0x16 are
 * reserved; they carry no measurements.
 */
#define ADJUSTOMETER_I2C_ADDR 0x57U

/* Feedback status bitmask. */
#define ADJ_STATUS_OK 0x00U
#define ADJ_STATUS_SIGNAL_LOST (1U << 0)
#define ADJ_STATUS_FUEL_TEMP_BROKEN (1U << 1)
#define ADJ_STATUS_BASELINE_PENDING (1U << 2)
#define ADJ_STATUS_VOLTAGE_BAD (1U << 3)

/*
 * Optional extension, version 2. Multi-byte values are big-endian.
 *
 * A writer publishes the complete block atomically with equal even sequence
 * bytes. The HAL freezes it for each read. Readers check both sequence bytes.
 * Signed deviation is derived from signal minus baseline at the receiver.
 */
#define ADJUSTOMETER_EXT_VERSION 2U
#define ADJUSTOMETER_REG_EXT_VERSION 0x05U
#define ADJUSTOMETER_REG_EXT_SEQ_BEGIN 0x06U
#define ADJUSTOMETER_REG_EXT_FLAGS 0x07U
#define ADJUSTOMETER_REG_SIGNAL_HZ 0x08U        /* uint32_t */
#define ADJUSTOMETER_REG_BASELINE_HZ 0x0CU      /* uint32_t */
#define ADJUSTOMETER_REG_CHIP_TEMP_DECI_C 0x10U /* int16_t */
#define ADJUSTOMETER_REG_EXT_SEQ_END 0x12U

#define ADJUSTOMETER_EXT_FLAG_SIGNAL_VALID (1U << 0)
#define ADJUSTOMETER_EXT_FLAG_BASELINE_VALID (1U << 1)
#define ADJUSTOMETER_EXT_FLAG_CHIP_TEMP_VALID (1U << 2)

#define ADJUSTOMETER_EXT_REG_START ADJUSTOMETER_REG_EXT_VERSION
#define ADJUSTOMETER_EXT_REG_COUNT                                             \
  (ADJUSTOMETER_REG_EXT_SEQ_END - ADJUSTOMETER_EXT_REG_START + 1U)

/** @brief Fast feedback address, independent of the diagnostic layout. */
#define ADJUSTOMETER_FEEDBACK_START 0x17U
#define ADJUSTOMETER_FEEDBACK_LEGACY_VERSION 1U
#define ADJUSTOMETER_FEEDBACK_VERSION 2U
/** @brief Version 2 stores Celsius plus this offset in byte 5. */
#define ADJUSTOMETER_FUEL_TEMP_OFFSET_C 40
/** @brief Accepted sensor range; separate from the control model's range. */
#define ADJUSTOMETER_FUEL_TEMP_MIN_C (-40)
#define ADJUSTOMETER_FUEL_TEMP_MAX_C 120
#define ADJUSTOMETER_FEEDBACK_BYTES 30U
#define ADJUSTOMETER_TOTAL_REG_COUNT                                           \
  (ADJUSTOMETER_FEEDBACK_START + ADJUSTOMETER_FEEDBACK_BYTES)
/** @brief Maximum accepted measurement age in microseconds. */
#define ADJUSTOMETER_FEEDBACK_MAX_AGE_US 20000U
