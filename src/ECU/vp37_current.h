#ifndef T_VP37_CURRENT
#define T_VP37_CURRENT

#include <JaszczurHAL.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Highest 12-bit ADC code. A sample at this code is treated as clipped. */
#define VP37_CURRENT_ADC_MAX_RAW 4095U
/** Histogram bin count, one per ADC code. */
#define VP37_CURRENT_ADC_BINS (VP37_CURRENT_ADC_MAX_RAW + 1U)
/**
 * Phase-capture buffer depth. The buffer overlays the histogram, so a phase
 * capture costs no extra RAM but discards any pending aggregate window.
 */
#define VP37_CURRENT_PHASE_SAMPLES 1024U
/** Largest number of PWM cycles a single phase capture reports on. */
#define VP37_CURRENT_PHASE_MAX_CYCLES 12U

/**
 * Bench conditions recorded with one current window.
 *
 * A measurement is only reproducible together with the state that produced it,
 * so every capture carries the commanded PWM, the position pair and the supply
 * and fuel readings used by the compensation path. Unknown fields stay
 * negative.
 */
typedef struct {
  int32_t pwmCommand; /**< Commanded MOSFET ON duty numerator, <0 unknown. */
  int32_t measuredHz; /**< Adjustometer position, <0 unknown. */
  int32_t desiredHz;  /**< Commanded position, <0 unknown. */
  float supplyVolts;  /**< ECU supply used by compensation, <=0 unknown. */
  float fuelTempC;    /**< Fuel temperature, not winding temperature. */
} VP37CurrentConditions;

/** Aggregated ADC0 samples from the VP37 MOSFET source shunt. */
typedef struct {
  uint32_t samples;
  uint32_t activeSamples;
  uint32_t maxActiveRun;
  uint32_t windowUs;
  uint32_t clippedSamples;
  uint16_t zeroRaw;
  bool zeroValid;
  uint16_t rawMin;
  uint16_t rawMax;
  uint16_t rawP05;
  uint16_t rawP25;
  uint16_t rawP50;
  uint16_t rawP75;
  uint16_t rawP95;
  float rawMean;
  float rawActiveMean;
  float peakVolts;
  float switchMeanAmps;
  float activeMeanAmps;
  float p05Amps;
  float p25Amps;
  float p50Amps;
  float p75Amps;
  float p95Amps;
  float peakAmps;
  float activePercent;
  /** RMS of the shunt current over the whole window, zero phase included. */
  float rmsShuntAmps;
  /** Mean power dissipated by the source resistor over the window. */
  float shuntPowerWatts;
  /** Consecutive active samples one ON phase should produce at this duty. */
  uint32_t expectedActiveRun;
  /** maxActiveRun divided by expectedActiveRun, zero when duty is unknown. */
  float activeRunRatio;
  /** True when the longest active run matches the commanded duty. */
  bool activeRunPlausible;
  VP37CurrentConditions conditions;
} VP37CurrentReading;

/** One raw sample of a PWM-phase-aligned capture. */
typedef struct {
  uint32_t timestampUs; /**< hal_micros() at the ADC read. */
  uint16_t rawSample;   /**< Zero-corrected ADC code. */
  uint8_t gateOn;       /**< 1 while the power stage drives the coil. */
  uint8_t clipped;      /**< 1 when the raw code hit the ADC end stop. */
} VP37CurrentPhaseSample;

/**
 * Per-cycle findings of a phase-aligned capture.
 *
 * `startAmps` is the current the shunt sees in the first ON sample. It carries
 * whatever the coil retained through the freewheel phase, so a value well above
 * zero means the coil never stops conducting and its mean current is closer to
 * `activeMeanAmps` than to `switchMeanAmps`.
 */
typedef struct {
  uint32_t cycles;         /**< Cycles accepted for the statistics. */
  uint32_t rejectedCycles; /**< Cycles dropped as incomplete or irregular. */
  uint32_t clippedSamples;
  uint32_t medianPeriodUs;
  uint32_t medianOnTimeUs;
  float startAmps;     /**< Median current in the first ON sample. */
  float endAmps;       /**< Median current in the last ON sample. */
  float riseAmpsPerMs; /**< Median ON-phase slope. */
  float chargeAmpMs;   /**< Median integral of i dt over the ON phase. */
  float rmsShuntAmps;  /**< RMS over the accepted cycles. */
  float shuntPowerWatts;
  /**
   * True when the ON phase starts from a substantial current, which indicates
   * continuous conduction through a freewheel path that bypasses the shunt.
   */
  bool continuousConduction;
  VP37CurrentConditions conditions;
} VP37CurrentPhaseResult;

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
 * @brief Read and clear the window, attaching the conditions that produced it.
 * @param conditions Bench state to record, or NULL to leave it unknown.
 * @param out Caller-owned result storage. Must not be NULL.
 * @return HAL_OK, HAL_EINVAL for NULL out, or HAL_EAGAIN without samples.
 */
hal_status_t
VP37_currentSenseSnapshotEx(const VP37CurrentConditions *conditions,
                            VP37CurrentReading *out);

/**
 * @brief Capture at least five 200 Hz PWM periods and return their summary.
 * @param out Caller-owned result storage. Must not be NULL.
 * @return HAL_OK or HAL_EINVAL for NULL.
 * @note Bench-only acquisition blocks its calling core. The nominal window is
 *       25 ms; the per-sample ADC and mutex cost stretches it to roughly 38 ms
 *       on RP2040, which is about 7.6 PWM periods.
 */
hal_status_t VP37_currentSenseCapture(VP37CurrentReading *out);

/**
 * @brief Capture one aggregate window and record the bench conditions with it.
 * @param conditions Bench state to record, or NULL to leave it unknown.
 * @param out Caller-owned result storage. Must not be NULL.
 * @return HAL_OK or HAL_EINVAL for NULL out.
 */
hal_status_t VP37_currentSenseCaptureEx(const VP37CurrentConditions *conditions,
                                        VP37CurrentReading *out);

/**
 * @brief Capture PWM-phase-aligned samples and reduce them to per-cycle facts.
 *
 * Waits for a gate turn-on edge, fills the phase buffer, then segments it into
 * PWM cycles. Answers what the aggregate window cannot: whether the coil
 * current decays to zero during the OFF phase.
 *
 * @param conditions Bench state to record, or NULL to leave it unknown.
 * @param out Caller-owned result storage. Must not be NULL.
 * @return HAL_OK, HAL_EINVAL for NULL out, HAL_ETIMEOUT without a gate edge,
 *         or HAL_EAGAIN when no complete cycle survived segmentation.
 * @note Overwrites the histogram, so any pending aggregate window is lost.
 */
hal_status_t
VP37_currentSensePhaseCapture(const VP37CurrentConditions *conditions,
                              VP37CurrentPhaseResult *out);

/**
 * @brief Expose the last phase capture for a raw dump.
 * @param out_count Receives the stored sample count. Must not be NULL.
 * @return Buffer pointer, or NULL when no phase capture has run.
 */
const VP37CurrentPhaseSample *VP37_currentPhaseSamples(uint32_t *out_count);

/**
 * @brief Convert a zero-corrected ADC code to shunt current.
 * @param raw Zero-corrected ADC code.
 * @return Current in amperes.
 */
float VP37_currentRawToAmps(uint16_t raw);

/**
 * @brief Find the bin holding a percentile of a histogram slice.
 * @param histogram Bin array indexed by ADC code. Must not be NULL.
 * @param bins Number of bins.
 * @param fromBin First bin counted, used to skip the sub-threshold codes.
 * @param population Samples contained in the counted slice. Must be non-zero.
 * @param percentile Requested percentile, 1..100.
 * @param out_bin Receives the bin. Must not be NULL.
 * @return HAL_OK, or HAL_EINVAL on a NULL or out-of-range argument.
 */
hal_status_t VP37_currentHistogramPercentile(const uint16_t *histogram,
                                             uint32_t bins, uint32_t fromBin,
                                             uint32_t population,
                                             uint8_t percentile,
                                             uint16_t *out_bin);

/**
 * @brief Consecutive active samples one ON phase should produce.
 * @param pwmCommand Commanded MOSFET ON duty numerator, <0 when unknown.
 * @param samples Samples collected in the window.
 * @param windowUs Window length in microseconds.
 * @return Expected run length, or zero when the duty or timing is unknown.
 */
uint32_t VP37_currentExpectedActiveRun(int32_t pwmCommand, uint32_t samples,
                                       uint32_t windowUs);

/**
 * @brief Segment phase samples into PWM cycles and reduce them.
 * @param samples Phase samples in acquisition order. Must not be NULL.
 * @param count Number of samples.
 * @param out Caller-owned result storage. Must not be NULL.
 * @return HAL_OK, HAL_EINVAL on NULL, or HAL_EAGAIN without a complete cycle.
 * @note Pure analysis, independent of the ADC and GPIO paths.
 */
hal_status_t VP37_currentPhaseAnalyze(const VP37CurrentPhaseSample *samples,
                                      uint32_t count,
                                      VP37CurrentPhaseResult *out);

#ifdef __cplusplus
}
#endif

#endif
