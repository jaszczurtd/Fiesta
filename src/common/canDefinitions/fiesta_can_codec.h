#pragma once

/** @file Scalar encodings used by Fiesta CAN payloads. */

#include <hal/core/hal_math.h>
#include <hal/core/jh_endian.h>

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

#ifdef __cplusplus
}
#endif
