#ifndef T_VP37_CURRENT
#define T_VP37_CURRENT

#include <JaszczurHAL.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Aggregated ADC0 samples from the VP37 MOSFET source shunt. */
typedef struct {
  uint32_t samples;
  uint32_t activeSamples;
  uint32_t maxActiveRun;
  uint32_t windowUs;
  uint16_t zeroRaw;
  bool zeroValid;
  uint16_t rawMin;
  uint16_t rawMax;
  uint16_t rawP95;
  float rawMean;
  float rawActiveMean;
  float peakVolts;
  float switchMeanAmps;
  float activeMeanAmps;
  float p95Amps;
  float peakAmps;
  float activePercent;
} VP37CurrentReading;

/**
 * @brief Configure ADC0, calibrate its PWM-off zero and clear samples.
 * @return None.
 * @pre The actuator is disabled and its PWM output is zero.
 */
void VP37_currentSenseInit(void);

/**
 * @brief Add one ADC0 source-shunt sample to the current telemetry window.
 * @return None.
 * @note Call from one owner context. The low-side source shunt generally sees
 *       MOSFET ON current, not the coil freewheel current during PWM OFF.
 */
void VP37_currentSenseSample(void);

/**
 * @brief Read and clear the current telemetry window.
 * @param out Caller-owned result storage. Must not be NULL.
 * @return HAL_OK, HAL_EINVAL for NULL, or HAL_EAGAIN without samples.
 */
hal_status_t VP37_currentSenseSnapshot(VP37CurrentReading *out);

/**
 * @brief Capture at least five 200 Hz PWM periods and return their summary.
 * @param out Caller-owned result storage. Must not be NULL.
 * @return HAL_OK or HAL_EINVAL for NULL.
 * @note Bench-only acquisition blocks its calling core for at least 25 ms.
 */
hal_status_t VP37_currentSenseCapture(VP37CurrentReading *out);

#ifdef __cplusplus
}
#endif

#endif
