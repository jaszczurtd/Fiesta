#pragma once

/** @file Scalar encodings used by Fiesta CAN payloads. */

#include <hal/core/hal_math.h>
#include <hal/core/jh_endian.h>

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

#ifdef __cplusplus
}
#endif
