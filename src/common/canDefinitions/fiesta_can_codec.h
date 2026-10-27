#pragma once

/** @file Scalar encodings used by Fiesta CAN payloads. */

#include <hal/core/hal_math.h>
#include <hal/core/jh_endian.h>
#include <hal/time/jh_calendar.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void fiesta_can_split_decimal_tenths(float value, int *whole,
                                                   int *tenths) {
  hal_math_split_decimal_tenths(value, whole, tenths);
}

static inline float fiesta_can_join_decimal_tenths(int whole, int tenths) {
  return hal_math_join_decimal_tenths(whole, tenths);
}

/**
 * @brief Store a temperature in a signed 16-bit big-endian field.
 * @param field Two payload bytes.
 * @param celsius Temperature; NaN or infinity means no measurement.
 * @return true for a measurement, stored in whole °C truncated toward zero
 *         and clamped to the int16_t range; false stores 0.
 */
static inline bool fiesta_can_put_temp_i16(uint8_t *field, float celsius) {
  int32_t value = 0;
  const bool measured = isfinite(celsius);
  if (measured) {
    const float clamped =
        hal_constrain(celsius, (float)INT16_MIN, (float)INT16_MAX);
    value = (int32_t)clamped;
  }
  jh_store_be16(field, (uint16_t)(int16_t)value);
  return measured;
}

/**
 * @brief Read a temperature stored by fiesta_can_put_temp_i16().
 * @param field Two payload bytes.
 * @return Whole °C, negative values included.
 */
static inline int16_t fiesta_can_get_temp_i16(const uint8_t *field) {
  return (int16_t)jh_load_be16(field);
}

/**
 * @brief Pack YYMMDD and HHMM into the 24-bit calendar format of 0x12E and
 *        0x130.
 * @param dateYYMMDD Two-digit year, month and day as YYMMDD.
 * @param timeHHMM Hour and minute as HHMM.
 * @return Packed value, or 0 for invalid month/day/time ranges. The year
 *         offset is clamped to the legacy 2020..2035 range.
 */
static inline uint32_t fiesta_can_pack_gps_datetime(uint32_t dateYYMMDD,
                                                    uint32_t timeHHMM) {
  const uint32_t yy = dateYYMMDD / 10000u;
  const uint32_t mm = (dateYYMMDD / 100u) % 100u;
  const uint32_t dd = dateYYMMDD % 100u;
  const uint32_t hh = timeHHMM / 100u;
  const uint32_t mi = timeHHMM % 100u;
  if (mm < 1u || mm > 12u || dd < 1u || dd > 31u || hh > 23u || mi > 59u)
    return 0u;
  const uint32_t yearOffset = yy < 20u ? 0u : (yy > 35u ? 15u : yy - 20u);
  return (yearOffset << 20) | (mm << 16) | (dd << 11) | (hh << 6) | mi;
}

/** Years the 24-bit calendar format of 0x12E and 0x130 can hold. */
#define FIESTA_CAN_DATETIME_MIN_YEAR 2020u
#define FIESTA_CAN_DATETIME_MAX_YEAR 2035u

/**
 * @brief Store UTC seconds in the 24-bit calendar format, to the minute.
 * @param field Three payload bytes, high byte first.
 * @param epoch UTC seconds.
 * @return true for a year from FIESTA_CAN_DATETIME_MIN_YEAR to
 *         FIESTA_CAN_DATETIME_MAX_YEAR; false stores zeros.
 */
static inline bool fiesta_can_put_utc_datetime(uint8_t *field, uint32_t epoch) {
  jh_calendar_datetime_t dt;
  uint32_t packed = 0u;
  const bool fits = jh_calendar_epoch_to_datetime(
                        epoch, FIESTA_CAN_DATETIME_MAX_YEAR, &dt) == HAL_OK &&
                    dt.year >= FIESTA_CAN_DATETIME_MIN_YEAR;
  if (fits) {
    packed =
        fiesta_can_pack_gps_datetime((uint32_t)(dt.year % 100u) * 10000u +
                                         (uint32_t)dt.month * 100u + dt.day,
                                     (uint32_t)dt.hour * 100u + dt.minute);
  }
  field[0] = (uint8_t)(packed >> 16);
  field[1] = (uint8_t)(packed >> 8);
  field[2] = (uint8_t)packed;
  return fits;
}

/**
 * @brief Read UTC seconds stored by fiesta_can_put_utc_datetime().
 * @param field Three payload bytes, high byte first.
 * @param second Seconds of the minute sent next to the field, 0..59.
 * @param outEpoch UTC seconds; left unchanged on failure.
 * @return true for a real date and time; false for the zero field, an
 *         impossible date or @p second above 59.
 */
static inline bool fiesta_can_get_utc_datetime(const uint8_t *field,
                                               uint8_t second,
                                               uint32_t *outEpoch) {
  const uint32_t packed = ((uint32_t)field[0] << 16) |
                          ((uint32_t)field[1] << 8) | (uint32_t)field[2];
  jh_calendar_datetime_t dt;
  dt.year = (uint16_t)(FIESTA_CAN_DATETIME_MIN_YEAR + (packed >> 20));
  dt.month = (uint8_t)((packed >> 16) & 0x0Fu);
  dt.day = (uint8_t)((packed >> 11) & 0x1Fu);
  dt.hour = (uint8_t)((packed >> 6) & 0x1Fu);
  dt.minute = (uint8_t)(packed & 0x3Fu);
  dt.second = second;
  dt.weekday = 0u;
  uint64_t epoch = 0u;
  const bool valid = jh_calendar_datetime_to_epoch(&dt, &epoch) == HAL_OK;
  if (valid) {
    *outEpoch = (uint32_t)epoch;
  }
  return valid;
}

#ifdef __cplusplus
}
#endif
