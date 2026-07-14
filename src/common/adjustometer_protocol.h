#pragma once

#include <stdint.h>

/*
 * Adjustometer I2C register map.
 *
 * Registers 0x00..0x04 are the original, control-path-compatible block and
 * must not change.  Registers 0x05 and above are optional diagnostic
 * telemetry.  An ECU may ignore the extension completely or reject it when
 * the version/sequence checks fail while still using the legacy block.
 */
#define ADJUSTOMETER_I2C_ADDR 0x57U

/* Legacy block: stable wire format used by the VP37 controller. */
#define ADJUSTOMETER_REG_PULSE_HI 0x00U
#define ADJUSTOMETER_REG_PULSE_LO 0x01U
#define ADJUSTOMETER_REG_VOLTAGE 0x02U
#define ADJUSTOMETER_REG_FUEL_TEMP 0x03U
#define ADJUSTOMETER_REG_STATUS 0x04U
#define ADJUSTOMETER_LEGACY_REG_COUNT 5U

/* STATUS register bitmask. */
#define ADJ_STATUS_OK 0x00U
#define ADJ_STATUS_SIGNAL_LOST (1U << 0)
#define ADJ_STATUS_FUEL_TEMP_BROKEN (1U << 1)
#define ADJ_STATUS_BASELINE_PENDING (1U << 2)
#define ADJ_STATUS_VOLTAGE_BAD (1U << 3)

/*
 * Optional extension, version 1.  Multi-byte values are big-endian.
 *
 * A writer marks SEQ_BEGIN odd while replacing the payload, then publishes
 * the same even value in SEQ_END and SEQ_BEGIN.  A reader accepts a snapshot
 * only when both sequence bytes match and are even.
 */
#define ADJUSTOMETER_EXT_VERSION 1U
#define ADJUSTOMETER_REG_EXT_VERSION 0x05U
#define ADJUSTOMETER_REG_EXT_SEQ_BEGIN 0x06U
#define ADJUSTOMETER_REG_EXT_FLAGS 0x07U
#define ADJUSTOMETER_REG_SIGNAL_HZ 0x08U        /* uint32_t */
#define ADJUSTOMETER_REG_BASELINE_HZ 0x0CU      /* uint32_t */
#define ADJUSTOMETER_REG_SIGNED_DELTA_HZ 0x10U  /* int32_t */
#define ADJUSTOMETER_REG_CHIP_TEMP_DECI_C 0x14U /* int16_t */
#define ADJUSTOMETER_REG_EXT_SEQ_END 0x16U

#define ADJUSTOMETER_EXT_FLAG_SIGNAL_VALID (1U << 0)
#define ADJUSTOMETER_EXT_FLAG_BASELINE_VALID (1U << 1)
#define ADJUSTOMETER_EXT_FLAG_CHIP_TEMP_VALID (1U << 2)

#define ADJUSTOMETER_EXT_REG_START ADJUSTOMETER_REG_EXT_VERSION
#define ADJUSTOMETER_EXT_REG_COUNT                                             \
  (ADJUSTOMETER_REG_EXT_SEQ_END - ADJUSTOMETER_EXT_REG_START + 1U)
#define ADJUSTOMETER_TOTAL_REG_COUNT (ADJUSTOMETER_REG_EXT_SEQ_END + 1U)
