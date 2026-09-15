#include "vp37_current.h"

#include "../common/fiesta_sensor_helpers.h"
#include "config.h"
#include "hardwareConfig.h"

#include <string.h>

#define VP37_CURRENT_ZERO_SAMPLES 256U
#define VP37_CURRENT_ZERO_MAX_RAW 128U
#define VP37_CURRENT_SAMPLE_DELAY_US 20U
#if VP37_PWM_FREQUENCY_HZ >= 1000
#define VP37_CURRENT_PULSE_SAMPLE_DELAY_US 4U
#else
#define VP37_CURRENT_PULSE_SAMPLE_DELAY_US VP37_CURRENT_SAMPLE_DELAY_US
#endif
#define VP37_CURRENT_PHASE_EDGE_TIMEOUT_US 30000U
#define VP37_CURRENT_PULSE_EDGE_GUARD_US 60U
#define VP37_CURRENT_PULSE_MIN_GUARDED_SAMPLES 8U
#define VP37_CURRENT_PULSE_PERIOD_TOLERANCE 0.20f
#define VP37_CURRENT_PULSE_TIMEOUT_US 15000U

static uint16_t s_currentZeroRaw;
static bool s_currentZeroValid;

/* The short ON ramp is nearly sorted; also used for the startup median. */
static void VP37_currentSortRaw(uint16_t *values, uint32_t count) {
  for (uint32_t i = 1U; i < count; i++) {
    const uint16_t key = values[i];
    uint32_t j = i;
    while ((j > 0U) && (values[j - 1U] > key)) {
      values[j] = values[j - 1U];
      j--;
    }
    values[j] = key;
  }
}

static float VP37_currentRawToVolts(int raw) {
  float volts = 0.0f;
  const hal_status_t status = fiesta_adc_to_voltage_ex(raw, 0.0f, 1.0f, &volts);
  if (status != HAL_OK) {
    volts = 0.0f;
  }
  return volts;
}

float VP37_currentRawToAmps(uint16_t raw) {
  return VP37_currentRawToVolts((int)raw) / VP37_CURRENT_SHUNT_OHMS;
}

static uint16_t VP37_currentReadRaw(void) {
  int raw = hal_adc_compensate_rp2040_12bit(hal_adc_read(ADC_VP37_CURRENT_PIN));
  if (raw < 0) {
    raw = 0;
  } else if (raw > (int)VP37_CURRENT_ADC_MAX_RAW) {
    raw = (int)VP37_CURRENT_ADC_MAX_RAW;
  } else {
    // Reading is already inside the supported 12-bit range.
  }
  return (uint16_t)raw;
}

/** Subtract the calibrated zero and report whether the code hit the end stop.
 */
static uint16_t VP37_currentCorrectedRaw(bool *out_clipped) {
  const uint16_t raw = VP37_currentReadRaw();
  if (out_clipped != NULL) {
    *out_clipped = raw >= VP37_CURRENT_ADC_MAX_RAW;
  }
  uint16_t offset = 0U;
  if (s_currentZeroValid) {
    offset = s_currentZeroRaw;
  }
  uint16_t sample = 0U;
  if (raw > offset) {
    sample = (uint16_t)(raw - offset);
  }
  return sample;
}

void VP37_currentSenseInit(void) {
  uint16_t samples[VP37_CURRENT_ZERO_SAMPLES];
  s_currentZeroRaw = 0U;
  s_currentZeroValid = false;
  (void)VP37_currentReadRaw();
  for (uint32_t i = 0U; i < COUNTOF(samples); i++) {
    samples[i] = VP37_currentReadRaw();
    hal_delay_us(VP37_CURRENT_SAMPLE_DELAY_US);
  }
  VP37_currentSortRaw(samples, COUNTOF(samples));
  s_currentZeroRaw = samples[COUNTOF(samples) / 2U];
  s_currentZeroValid = s_currentZeroRaw <= VP37_CURRENT_ZERO_MAX_RAW;
}

static bool VP37_currentPeriodPlausible(uint32_t periodUs) {
  const uint32_t expectedPeriodUs = 1000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ;
  const float lowPeriod =
      (float)expectedPeriodUs * (1.0f - VP37_CURRENT_PULSE_PERIOD_TOLERANCE);
  const float highPeriod =
      (float)expectedPeriodUs * (1.0f + VP37_CURRENT_PULSE_PERIOD_TOLERANCE);
  return ((float)periodUs >= lowPeriod) && ((float)periodUs <= highPeriod);
}

hal_status_t VP37_currentPulseAnalyze(const VP37CurrentPhaseSample *samples,
                                      uint32_t count, uint32_t cycleStartUs,
                                      uint32_t onTimeUs, uint32_t periodUs,
                                      VP37CurrentPulseResult *out) {
  if ((samples == NULL) || (out == NULL) || (count == 0U) ||
      (onTimeUs < (2U * VP37_CURRENT_PULSE_EDGE_GUARD_US)) ||
      (periodUs <= onTimeUs)) {
    return HAL_EINVAL;
  }

  if (count > VP37_CURRENT_PULSE_SAMPLES) {
    return HAL_EOVERFLOW;
  }

  (void)memset(out, 0, sizeof(*out));
  out->samples = count;
  out->cycleStartUs = cycleStartUs;
  out->periodUs = periodUs;
  out->onTimeUs = onTimeUs;
  out->zeroRaw = s_currentZeroRaw;
  out->zeroValid = s_currentZeroValid;

  uint16_t guarded[VP37_CURRENT_PULSE_SAMPLES];
  uint32_t guardedCount = 0U;
  uint16_t peakRaw = 0U;
  for (uint32_t i = 0U; i < count; i++) {
    if (samples[i].rawSample > VP37_CURRENT_ADC_MAX_RAW) {
      return HAL_EINVAL;
    }
    if (samples[i].clipped != 0U) {
      out->clippedSamples++;
    }
    if (samples[i].rawSample > peakRaw) {
      peakRaw = samples[i].rawSample;
    }
    const uint32_t phaseUs = samples[i].timestampUs - cycleStartUs;
    if ((samples[i].gateOn != 0U) &&
        (phaseUs >= VP37_CURRENT_PULSE_EDGE_GUARD_US) &&
        (phaseUs <= (onTimeUs - VP37_CURRENT_PULSE_EDGE_GUARD_US))) {
      guarded[guardedCount] = samples[i].rawSample;
      guardedCount++;
    }
  }
  out->guardedSamples = guardedCount;
  out->peakAmps = VP37_currentRawToAmps(peakRaw);
  if (guardedCount < VP37_CURRENT_PULSE_MIN_GUARDED_SAMPLES) {
    return HAL_EAGAIN;
  }

  VP37_currentSortRaw(guarded, guardedCount);
  const uint32_t p95Rank = (((guardedCount * 95U) + 99U) / 100U) - 1U;
  const uint16_t p95Raw = guarded[p95Rank];
  uint32_t winsorizedSum = 0U;
  for (uint32_t i = 0U; i < guardedCount; i++) {
    winsorizedSum += (guarded[i] < p95Raw) ? guarded[i] : p95Raw;
  }
  const uint16_t meanRaw =
      (uint16_t)((winsorizedSum + (guardedCount / 2U)) / guardedCount);
  out->meanAmps = VP37_currentRawToAmps(meanRaw);
  out->p95Amps = VP37_currentRawToAmps(p95Raw);

  uint64_t pwm = ((uint64_t)onTimeUs * (uint64_t)PWM_RESOLUTION) +
                 ((uint64_t)periodUs / 2U);
  pwm /= periodUs;
  if (pwm > (uint64_t)PWM_RESOLUTION) {
    pwm = PWM_RESOLUTION;
  }
  out->pwmCommand = (int32_t)pwm;
  out->waveformValid = out->zeroValid && (out->clippedSamples == 0U) &&
                       VP37_currentPeriodPlausible(periodUs);
  return HAL_OK;
}

hal_status_t VP37_currentSensePulseCapture(VP37CurrentPulseResult *out) {
  static VP37CurrentPhaseSample s_pulseSamples[VP37_CURRENT_PULSE_SAMPLES];
  if (out == NULL) {
    return HAL_EINVAL;
  }
  (void)memset(out, 0, sizeof(*out));
  out->zeroRaw = s_currentZeroRaw;
  out->zeroValid = s_currentZeroValid;
  if (!s_currentZeroValid) {
    return HAL_ESTATE;
  }

  const uint32_t waitStartUs = hal_micros();
  bool previousOn = !hal_gpio_read(PIO_VP37_RPM);
  bool aligned = false;
  while (!hal_elapsed_u32(hal_micros(), waitStartUs,
                          VP37_CURRENT_PHASE_EDGE_TIMEOUT_US)) {
    const bool gateOn = !hal_gpio_read(PIO_VP37_RPM);
    if (gateOn && !previousOn) {
      aligned = true;
      break;
    }
    previousOn = gateOn;
    hal_delay_us(1U);
  }
  if (!aligned) {
    return HAL_ETIMEOUT;
  }

  const uint32_t cycleStartUs = hal_micros();
  uint32_t onTimeUs = 0U;
  uint32_t periodUs = 0U;
  uint32_t count = 0U;
  bool sawOff = false;
  uint32_t supplySum[2] = {0U, 0U};
  uint32_t supplyCount[2] = {0U, 0U};
  bool supplyValid = true;
  while (!hal_elapsed_u32(hal_micros(), cycleStartUs,
                          VP37_CURRENT_PULSE_TIMEOUT_US)) {
    const bool gateOn = !hal_gpio_read(PIO_VP37_RPM);
    const uint32_t nowUs = hal_micros();
    if (!gateOn) {
      if (!sawOff) {
        onTimeUs = nowUs - cycleStartUs;
        sawOff = true;
      }
    } else if (sawOff) {
      periodUs = nowUs - cycleStartUs;
      break;
    } else {
      if (count >= COUNTOF(s_pulseSamples)) {
        return HAL_EOVERFLOW;
      }
      bool clipped = false;
      (void)hal_adc_read(ADC_VP37_CURRENT_PIN);
      const uint16_t sample = VP37_currentCorrectedRaw(&clipped);
      s_pulseSamples[count].timestampUs = hal_micros();
      s_pulseSamples[count].rawSample = sample;
      s_pulseSamples[count].gateOn = 1U;
      s_pulseSamples[count].clipped = clipped ? 1U : 0U;
      count++;
    }
    // Each phase gets its own mean because ON conversions take longer.
    (void)hal_adc_read(ADC_VOLT_PIN);
    const int supplyRaw =
        hal_adc_compensate_rp2040_12bit(hal_adc_read(ADC_VOLT_PIN));
    if ((supplyRaw > 0) && (supplyRaw < (int)VP37_CURRENT_ADC_MAX_RAW)) {
      const uint32_t phase = gateOn ? 1U : 0U;
      supplySum[phase] += (uint32_t)supplyRaw;
      supplyCount[phase]++;
    } else {
      supplyValid = false;
    }
    hal_delay_us(VP37_CURRENT_PULSE_SAMPLE_DELAY_US);
  }
  if ((onTimeUs == 0U) || (periodUs == 0U)) {
    return HAL_ETIMEOUT;
  }
  const hal_status_t status = VP37_currentPulseAnalyze(
      s_pulseSamples, count, cycleStartUs, onTimeUs, periodUs, out);
  if ((status == HAL_OK) && supplyValid &&
      VP37_currentPeriodPlausible(periodUs) && (supplyCount[0] > 0U) &&
      (supplyCount[1] > 0U)) {
    const float onFraction = (float)onTimeUs / (float)periodUs;
    const float rawMean =
        ((float)supplySum[1] / (float)supplyCount[1]) * onFraction +
        ((float)supplySum[0] / (float)supplyCount[0]) * (1.0f - onFraction);
    out->supplyValid = fiesta_adc_to_voltage_ex(
                           (int)(rawMean + 0.5f), (float)V_DIVIDER_R1,
                           (float)V_DIVIDER_R2, &out->supplyVolts) == HAL_OK;
  }
  return status;
}
