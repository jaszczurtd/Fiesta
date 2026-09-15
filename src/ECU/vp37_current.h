#ifndef T_VP37_CURRENT
#define T_VP37_CURRENT

#include <JaszczurHAL.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Highest 12-bit ADC code; reaching it marks a clipped observation. */
#define VP37_CURRENT_ADC_MAX_RAW 4095U
/** Maximum ON samples in one measured PWM period. */
#define VP37_CURRENT_PULSE_SAMPLES 256U
/** Interval between passive observations on core 0, in milliseconds. */
#define VP37_CURRENT_OBSERVATION_MS 20U

/** One raw sample of a PWM-phase-aligned capture. */
typedef struct {
  uint32_t timestampUs; /**< hal_micros() at the ADC read. */
  uint16_t rawSample;   /**< Zero-corrected ADC code. */
  uint8_t gateOn;       /**< 1 while the power stage drives the coil. */
  uint8_t clipped;      /**< 1 when the raw code hit the ADC end stop. */
} VP37CurrentPhaseSample;

/** Edge-filtered current from one complete, gate-aligned PWM period. */
typedef struct {
  uint32_t samples;        /**< ON-phase samples acquired. */
  uint32_t guardedSamples; /**< Samples left after both edge guards. */
  uint32_t clippedSamples;
  uint32_t cycleStartUs;
  uint32_t periodUs;
  uint32_t onTimeUs;
  int32_t pwmCommand; /**< Duty reconstructed from measured gate timing. */
  uint16_t zeroRaw;
  bool zeroValid;
  float meanAmps; /**< P95-winsorized mean inside the guarded ON phase. */
  float p95Amps;  /**< Guarded ON-phase 95th percentile. */
  float peakAmps; /**< Unfiltered ON-phase maximum for diagnostics only. */
  bool waveformValid;
  float supplyVolts; /**< Local supply averaged over the complete PWM period. */
  bool supplyValid;  /**< Independent of current amplitude and clipping. */
} VP37CurrentPulseResult;

/**
 * @brief Calibrate the source-shunt ADC zero with PWM disabled.
 * @pre Actuator output is zero; call once on core 0 before pump calibration.
 * @return None. An implausible zero remains visible as invalid telemetry.
 */
void VP37_currentSenseInit(void);

/**
 * @brief Observe one complete PWM period, including ON-edge guards.
 * @param out Non-NULL caller-owned result; cleared even on acquisition failure.
 * @return HAL_OK, HAL_EINVAL (NULL), HAL_ESTATE (invalid zero), HAL_ETIMEOUT
 * (missing edges), HAL_EOVERFLOW (full buffer), or HAL_EAGAIN (few samples).
 * @note Single owner, core 0. Waits at most 30 ms for an edge and 15 ms for
 * a complete period. Never changes PWM or position-controller state.
 */
hal_status_t VP37_currentSensePulseCapture(VP37CurrentPulseResult *out);

/**
 * @brief Reduce zero-corrected ON samples from one measured PWM period.
 * @param samples Non-NULL samples in acquisition order.
 * @param count Number of samples, 1..VP37_CURRENT_PULSE_SAMPLES.
 * @param cycleStartUs Turn-on timestamp; uint32_t wrap is supported.
 * @param onTimeUs ON duration, at least 120 us and less than periodUs.
 * @param periodUs Measured turn-on to turn-on period in microseconds.
 * @param out Non-NULL result, including the startup zero state.
 * @return HAL_OK, HAL_EINVAL (invalid input), HAL_EOVERFLOW (count too large),
 * or HAL_EAGAIN (too few samples after the 60 us edge guards).
 * @note A clipped or irregular waveform is reported with waveformValid=false.
 * Mean and P95 describe the guarded ON phase, not the freewheel current.
 */
hal_status_t VP37_currentPulseAnalyze(const VP37CurrentPhaseSample *samples,
                                      uint32_t count, uint32_t cycleStartUs,
                                      uint32_t onTimeUs, uint32_t periodUs,
                                      VP37CurrentPulseResult *out);

/**
 * @brief Convert a zero-corrected 12-bit ADC code to source-shunt amperes.
 * @param raw ADC code, 0..4095.
 * @return Current in amperes using the configured source-shunt resistance.
 */
float VP37_currentRawToAmps(uint16_t raw);

#ifdef __cplusplus
}
#endif
#endif
