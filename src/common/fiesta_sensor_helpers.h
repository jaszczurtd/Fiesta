#pragma once

/** @file Fiesta-specific policies for the generic HAL ADC and NTC helpers. */

#include <hal/analog/hal_adc_utils.h>
#include <hal/temperature/hal_ntc.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read a Fiesta ADC input using the historical RP2040 sampling policy.
 *
 * This keeps the former tools helper semantics explicit: discard one reading,
 * collect four samples ten microseconds apart and compensate the RP2040
 * 12-bit ADC transfer gaps.
 */
static inline hal_status_t fiesta_adc_read_average_ex(uint8_t pin,
                                                      float *out_average) {
  const hal_adc_average_config_t config = {
      pin,  (uint16_t)HAL_ADC_UTIL_DEFAULT_SAMPLES, 10u,
      true, hal_adc_compensate_rp2040_12bit,
  };
  return hal_adc_read_average_ex(&config, out_average);
}

/** @brief Convert a rounded ADC code using Fiesta's 3.3 V divider policy. */
static inline hal_status_t fiesta_adc_to_voltage_ex(int raw,
                                                    float high_side_resistance,
                                                    float low_side_resistance,
                                                    float *out_voltage) {
  return hal_adc_raw_to_voltage_ex(raw, 3.3f, HAL_ADC_UTIL_DEFAULT_BITS,
                                   high_side_resistance, low_side_resistance,
                                   out_voltage);
}

/**
 * @brief Read a Fiesta NTC input with the historical endpoint clamping.
 */
static inline hal_status_t
fiesta_ntc_read_temperature_ex(uint8_t pin, float nominal_resistance,
                               float series_resistance, float *out_celsius) {
  if (out_celsius == NULL) {
    return HAL_EINVAL;
  }

  float average = 0.0f;
  hal_status_t status = fiesta_adc_read_average_ex(pin, &average);
  if (status != HAL_OK) {
    return status;
  }

  const float full_scale =
      (float)((UINT32_C(1) << HAL_ADC_UTIL_DEFAULT_BITS) - 1u);
  if (average >= full_scale) {
    average = full_scale - 1.0f;
  }
  if (average <= 0.0f) {
    average = 1.0f;
  }

  const hal_ntc_beta_config_t config = {
      nominal_resistance,
      series_resistance,
      HAL_NTC_DEFAULT_BETA,
      HAL_NTC_DEFAULT_NOMINAL_C,
  };
  return hal_ntc_temperature_from_adc_ex(average, full_scale, &config,
                                         out_celsius);
}

#ifdef __cplusplus
}
#endif
