#pragma once

/** @file Fiesta-specific policies for the generic HAL ADC and NTC helpers. */

#include <hal/analog/hal_adc_utils.h>
#include <hal/temperature/hal_ntc.h>
#if defined(HAL_ENABLE_ADC_SCAN)
#include <hal/analog/hal_adc_scan.h>
#endif

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read a Fiesta ADC input with an explicit spacing between samples.
 *
 * Discard one reading, collect four samples @p sample_delay_us apart and
 * compensate the RP2040 12-bit ADC transfer gaps. A module whose converter is
 * driven by a DMA scan passes at least one scan frame period, so the samples
 * come from distinct frames; a polled converter converts on every read and ten
 * microseconds suffice.
 */
static inline hal_status_t
fiesta_adc_read_average_spaced_ex(uint8_t pin, uint16_t sample_delay_us,
                                  float *out_average) {
  const hal_adc_average_config_t config = {
      pin,  (uint16_t)HAL_ADC_UTIL_DEFAULT_SAMPLES, sample_delay_us,
      true, hal_adc_compensate_rp2040_12bit,
  };
  return hal_adc_read_average_ex(&config, out_average);
}

/**
 * @brief Read a Fiesta ADC input using the historical RP2040 sampling policy.
 *
 * This keeps the former tools helper semantics explicit: discard one reading,
 * collect four samples ten microseconds apart and compensate the RP2040
 * 12-bit ADC transfer gaps.
 */
static inline hal_status_t fiesta_adc_read_average_ex(uint8_t pin,
                                                      float *out_average) {
  return fiesta_adc_read_average_spaced_ex(pin, 10u, out_average);
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
 * @brief Spacing between averaged ADC samples [us].
 *
 * One scan frame while the HAL ADC scan runs, so every sample comes from a
 * different frame; ten microseconds for a polled converter.
 */
static inline uint16_t fiesta_adc_sample_spacing_us(void) {
  uint32_t spacing = 10U;
#if defined(HAL_ENABLE_ADC_SCAN)
  if (hal_adc_scan_is_running()) {
    const uint32_t frameUs = (hal_adc_scan_frame_period_ns() + 999U) / 1000U;
    if (frameUs > spacing) {
      spacing = frameUs;
    }
  }
#endif
  return (uint16_t)spacing;
}

/**
 * @brief Voltage in front of a divider: source -> R1 -> @p pin -> R2 -> ground.
 *
 * Averaged with fiesta_adc_sample_spacing_us() between samples.
 * @return The voltage [V], or 0 V when the read or the conversion fails.
 */
static inline float fiesta_adc_read_divided_volts(uint8_t pin,
                                                  float high_side_resistance,
                                                  float low_side_resistance) {
  float average = 0.0f;
  float voltage = 0.0f;
  if ((fiesta_adc_read_average_spaced_ex(pin, fiesta_adc_sample_spacing_us(),
                                         &average) != HAL_OK) ||
      (fiesta_adc_to_voltage_ex((int)(average + 0.5f), high_side_resistance,
                                low_side_resistance, &voltage) != HAL_OK)) {
    voltage = 0.0f;
  }
  return voltage;
}

/**
 * @brief Read a Fiesta NTC input with the historical endpoint clamping and an
 * explicit spacing between samples.
 */
static inline hal_status_t fiesta_ntc_read_temperature_spaced_ex(
    uint8_t pin, uint16_t sample_delay_us, float nominal_resistance,
    float series_resistance, float *out_celsius) {
  if (out_celsius == NULL) {
    return HAL_EINVAL;
  }

  float average = 0.0f;
  hal_status_t status =
      fiesta_adc_read_average_spaced_ex(pin, sample_delay_us, &average);
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

/**
 * @brief Read a Fiesta NTC input with the historical endpoint clamping.
 */
static inline hal_status_t
fiesta_ntc_read_temperature_ex(uint8_t pin, float nominal_resistance,
                               float series_resistance, float *out_celsius) {
  return fiesta_ntc_read_temperature_spaced_ex(pin, 10u, nominal_resistance,
                                               series_resistance, out_celsius);
}

#ifdef __cplusplus
}
#endif
