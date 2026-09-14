#include "vp37_current.h"

#include "../common/fiesta_sensor_helpers.h"
#include "hardwareConfig.h"

#include <limits.h>
#include <string.h>

#define VP37_CURRENT_ACTIVE_THRESHOLD_RAW 64U
#define VP37_CURRENT_MIN_ACTIVE_SAMPLES 8U
#define VP37_CURRENT_MIN_ACTIVE_RUN 4U
#define VP37_CURRENT_ADC_MAX_RAW 4095U
#define VP37_CURRENT_ZERO_SAMPLES 256U
#define VP37_CURRENT_ZERO_MAX_RAW 128U
#define VP37_CURRENT_CAPTURE_SAMPLES 1250U
#define VP37_CURRENT_SAMPLE_DELAY_US 20U

typedef struct {
  uint64_t rawSum;
  uint64_t rawActiveSum;
  uint32_t samples;
  uint32_t activeSamples;
  uint32_t activeRun;
  uint32_t maxActiveRun;
  uint32_t startedUs;
  uint16_t rawMin;
  uint16_t rawMax;
  uint16_t histogram[VP37_CURRENT_ADC_MAX_RAW + 1U];
  bool started;
} vp37_current_state_t;

static vp37_current_state_t s_currentState;
static uint16_t s_currentZeroRaw;
static bool s_currentZeroValid;

static void VP37_currentSenseReset(void) {
  (void)memset(&s_currentState, 0, sizeof(s_currentState));
  s_currentState.rawMin = UINT16_MAX;
}

static float VP37_currentRawToVolts(int raw) {
  float volts = 0.0f;
  const hal_status_t status = fiesta_adc_to_voltage_ex(raw, 0.0f, 1.0f, &volts);
  if (status != HAL_OK) {
    volts = 0.0f;
  }
  return volts;
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

void VP37_currentSenseInit(void) {
  VP37_currentSenseReset();
  s_currentZeroRaw = 0U;
  s_currentZeroValid = false;

  // Configure GPIO26 as an ADC input, then find the PWM-off median. Reject an
  // implausibly high zero so a wiring fault remains visible in telemetry.
  (void)VP37_currentReadRaw();
  for (uint32_t i = 0U; i < VP37_CURRENT_ZERO_SAMPLES; i++) {
    const uint16_t sample = VP37_currentReadRaw();
    s_currentState.histogram[sample]++;
    hal_delay_us(VP37_CURRENT_SAMPLE_DELAY_US);
  }

  const uint32_t medianTarget = (VP37_CURRENT_ZERO_SAMPLES / 2U) + 1U;
  uint32_t cumulativeSamples = 0U;
  uint16_t zeroCandidate = 0U;
  for (uint32_t raw = 0U; raw <= VP37_CURRENT_ADC_MAX_RAW; raw++) {
    cumulativeSamples += s_currentState.histogram[raw];
    if (cumulativeSamples >= medianTarget) {
      zeroCandidate = (uint16_t)raw;
      break;
    }
  }
  s_currentZeroRaw = zeroCandidate;
  s_currentZeroValid = zeroCandidate <= VP37_CURRENT_ZERO_MAX_RAW;
  VP37_currentSenseReset();
}

void VP37_currentSenseSample(void) {
  const uint16_t raw = VP37_currentReadRaw();
  uint16_t offset = 0U;
  if (s_currentZeroValid) {
    offset = s_currentZeroRaw;
  }
  uint16_t sample = 0U;
  if (raw > offset) {
    sample = (uint16_t)(raw - offset);
  }

  if (!s_currentState.started) {
    s_currentState.startedUs = hal_micros();
    s_currentState.started = true;
  }
  if (s_currentState.samples >= UINT16_MAX) {
    return;
  }

  s_currentState.rawSum += sample;
  s_currentState.samples++;
  if (sample < s_currentState.rawMin) {
    s_currentState.rawMin = sample;
  }
  if (sample > s_currentState.rawMax) {
    s_currentState.rawMax = sample;
  }
  if (s_currentState.histogram[sample] != UINT16_MAX) {
    s_currentState.histogram[sample]++;
  }
  if (sample >= VP37_CURRENT_ACTIVE_THRESHOLD_RAW) {
    s_currentState.rawActiveSum += sample;
    s_currentState.activeSamples++;
    s_currentState.activeRun++;
    if (s_currentState.activeRun > s_currentState.maxActiveRun) {
      s_currentState.maxActiveRun = s_currentState.activeRun;
    }
  } else {
    s_currentState.activeRun = 0U;
  }
}

hal_status_t VP37_currentSenseSnapshot(VP37CurrentReading *out) {
  if (out == NULL) {
    return HAL_EINVAL;
  }
  if (s_currentState.samples == 0U) {
    return HAL_EAGAIN;
  }

  (void)memset(out, 0, sizeof(*out));
  out->samples = s_currentState.samples;
  out->activeSamples = s_currentState.activeSamples;
  out->maxActiveRun = s_currentState.maxActiveRun;
  out->windowUs = hal_micros() - s_currentState.startedUs;
  out->zeroRaw = s_currentZeroRaw;
  out->zeroValid = s_currentZeroValid;
  out->rawMin = s_currentState.rawMin;
  out->rawMax = s_currentState.rawMax;
  out->rawMean = (float)s_currentState.rawSum / (float)s_currentState.samples;

  uint64_t meanRawWide = s_currentState.rawSum + (s_currentState.samples / 2U);
  meanRawWide /= s_currentState.samples;
  const uint16_t meanRaw = (uint16_t)meanRawWide;
  const float meanVolts = VP37_currentRawToVolts((int)meanRaw);
  out->peakVolts = VP37_currentRawToVolts(s_currentState.rawMax);
  out->peakAmps = out->peakVolts / VP37_CURRENT_SHUNT_OHMS;

  if (s_currentState.activeSamples != 0U) {
    out->activePercent = ((float)s_currentState.activeSamples * 100.0f) /
                         (float)s_currentState.samples;
  }

  if (s_currentZeroValid &&
      (s_currentState.activeSamples >= VP37_CURRENT_MIN_ACTIVE_SAMPLES) &&
      (s_currentState.maxActiveRun >= VP37_CURRENT_MIN_ACTIVE_RUN)) {
    out->switchMeanAmps = meanVolts / VP37_CURRENT_SHUNT_OHMS;
    out->rawActiveMean = (float)s_currentState.rawActiveSum /
                         (float)s_currentState.activeSamples;
    uint64_t activeMeanRawWide =
        s_currentState.rawActiveSum + (s_currentState.activeSamples / 2U);
    activeMeanRawWide /= s_currentState.activeSamples;
    const uint16_t activeMeanRaw = (uint16_t)activeMeanRawWide;
    out->activeMeanAmps =
        VP37_currentRawToVolts((int)activeMeanRaw) / VP37_CURRENT_SHUNT_OHMS;
    const uint32_t percentileTarget =
        (uint32_t)((((uint64_t)s_currentState.activeSamples * 95U) + 99U) /
                   100U);
    uint32_t percentileCount = 0U;
    for (uint32_t raw = VP37_CURRENT_ACTIVE_THRESHOLD_RAW;
         raw <= (uint32_t)s_currentState.rawMax; raw++) {
      percentileCount += s_currentState.histogram[raw];
      if (percentileCount >= percentileTarget) {
        out->rawP95 = (uint16_t)raw;
        break;
      }
    }
    out->p95Amps =
        VP37_currentRawToVolts(out->rawP95) / VP37_CURRENT_SHUNT_OHMS;
  }

  VP37_currentSenseReset();
  return HAL_OK;
}

hal_status_t VP37_currentSenseCapture(VP37CurrentReading *out) {
  if (out == NULL) {
    return HAL_EINVAL;
  }

  VP37_currentSenseReset();
  for (uint32_t i = 0U; i < VP37_CURRENT_CAPTURE_SAMPLES; i++) {
    VP37_currentSenseSample();
    hal_delay_us(VP37_CURRENT_SAMPLE_DELAY_US);
  }
  return VP37_currentSenseSnapshot(out);
}
