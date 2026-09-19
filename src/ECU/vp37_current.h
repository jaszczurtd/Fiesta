#ifndef T_VP37_CURRENT
#define T_VP37_CURRENT

#include <JaszczurHAL.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Highest 12-bit ADC code; reaching it marks a clipped observation. */
#define VP37_CURRENT_ADC_MAX_RAW 4095U
/** Interval between `VP37 IPULSE` reports on core 0, in milliseconds. */
#define VP37_CURRENT_REPORT_MS 20U

/* The shunt, the sensor multiplexer and the supply divider share one
   hardware-paced scan; every sample then has a known position in time. */
#define VP37_CURRENT_SCAN_PINS 3U
#if VP37_PWM_FREQUENCY_HZ >= 1000
#define VP37_CURRENT_SCAN_CONVERSION_NS 2000U
#else
#define VP37_CURRENT_SCAN_CONVERSION_NS 8000U
#endif
/** Nominal time between two samples of one pin. */
#define VP37_CURRENT_SCAN_FRAME_NS                                             \
  (VP37_CURRENT_SCAN_CONVERSION_NS * VP37_CURRENT_SCAN_PINS)
/** Maximum ON samples in one measured PWM period: a whole period of frames
    plus a margin for edge quantization, so any PWM frequency fits. */
#define VP37_CURRENT_PULSE_SAMPLES                                             \
  ((((1000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ) * 1000U) /                   \
    VP37_CURRENT_SCAN_FRAME_NS) +                                              \
   8U)
/** Completed DMA blocks arrive slightly slower than the 5 ms control step. */
#define VP37_CURRENT_SCAN_BLOCK_NS 6000000U
/** Retained history spans 2.25 PWM periods so a complete rise-to-rise period
    remains available at any phase. At high PWM rates retain at least one
    complete DMA block. */
#define VP37_CURRENT_SCAN_PERIODS_NS                                           \
  ((9U * (1000000000U / (uint32_t)VP37_PWM_FREQUENCY_HZ)) / 4U)
#define VP37_CURRENT_SCAN_HISTORY_NS                                           \
  ((VP37_CURRENT_SCAN_PERIODS_NS > VP37_CURRENT_SCAN_BLOCK_NS)                 \
       ? VP37_CURRENT_SCAN_PERIODS_NS                                          \
       : VP37_CURRENT_SCAN_BLOCK_NS)
#define VP37_CURRENT_SCAN_BLOCK_FRAMES                                         \
  ((VP37_CURRENT_SCAN_BLOCK_NS + VP37_CURRENT_SCAN_FRAME_NS - 1U) /            \
   VP37_CURRENT_SCAN_FRAME_NS)
/** Capacity of the contiguous history consumed by the reducer. */
#define VP37_CURRENT_SCAN_HISTORY_FRAMES                                       \
  ((VP37_CURRENT_SCAN_HISTORY_NS + VP37_CURRENT_SCAN_FRAME_NS - 1U) /          \
   VP37_CURRENT_SCAN_FRAME_NS)
/** Gate detection from the source-shunt waveform, with hysteresis. An edge
    counts once the new level holds for the confirmation time; the excursions
    seen in the OFF phase at 200 Hz fit in one 24 us frame, so the time, not
    the frame count, is what rejects them at any frame period. */
#define VP37_CURRENT_GATE_ON_AMPS 0.5f
#define VP37_CURRENT_GATE_OFF_AMPS 0.25f
#define VP37_CURRENT_GATE_CONFIRM_NS 72000U
#define VP37_CURRENT_GATE_CONFIRM_FRAMES                                       \
  ((VP37_CURRENT_GATE_CONFIRM_NS + VP37_CURRENT_SCAN_FRAME_NS - 1U) /          \
   VP37_CURRENT_SCAN_FRAME_NS)

/** One raw sample of a PWM-phase-aligned capture. */
typedef struct {
  uint32_t timestampUs; /**< hal_micros() at the ADC read. */
  uint16_t rawSample;   /**< Zero-corrected ADC code. */
  uint8_t gateOn;       /**< 1 while the power stage drives the coil. */
  uint8_t clipped;      /**< 1 when the raw code hit the ADC end stop. */
} VP37CurrentPhaseSample;

/** Gate-aligned current and supply, plus the newest supply-only window. */
typedef struct {
  uint32_t samples;        /**< ON-phase samples acquired. */
  uint32_t guardedSamples; /**< Samples left after both edge guards. */
  uint32_t clippedSamples;
  uint32_t glitches; /**< Level excursions shorter than the edge confirmation
                        anywhere in the block; not gates, but counted. */
  uint32_t cycleStartUs;
  uint32_t periodUs;
  uint32_t onTimeUs;
  int32_t pwmCommand; /**< Duty reconstructed from measured gate timing. */
  uint16_t zeroRaw;
  bool zeroValid;
  float meanAmps;     /**< P95-winsorized mean inside the guarded ON phase. */
  float p95Amps;      /**< Guarded ON-phase 95th percentile. */
  float peakAmps;     /**< Unfiltered ON-phase maximum for diagnostics only. */
  bool waveformValid; /**< False keeps meanAmps, p95Amps and peakAmps unusable;
                         a rejected capture reports zero, not zero amperes. */
  float supplyVolts; /**< Local supply averaged over the complete PWM period. */
  uint32_t
      supplySamples; /**< Accepted supply conversions across both phases. */
  bool supplyValid;  /**< Own sample budget and timing rule; a rejected current
                        waveform does not invalidate the supply mean. */
  float supplyLatestVolts; /**< Supply mean over the newest one-period window,
                              independent of the current waveform. */
  uint32_t supplyLatestUs; /**< Center of that window in hal_micros() time;
                              uint32_t wrap is supported. */
  bool supplyLatestValid;  /**< A complete window with valid supply samples. */
} VP37CurrentPulseResult;

/** Contiguous completed scan frames as seen by the reducer. */
typedef struct {
  const uint16_t *samples; /**< Interleaved frames, one sample per pin. */
  uint32_t frames;
  uint8_t pinCount;
  uint8_t shuntPosition;  /**< Position of the shunt inside a frame. */
  uint8_t supplyPosition; /**< Position of the supply divider inside a frame. */
  uint32_t frameNs;       /**< Time between two samples of one pin. */
  uint32_t startUs;       /**< hal_micros() of the first frame; may wrap. */
} VP37CurrentScanBlock;

/**
 * @brief Calibrate the source-shunt ADC zero with PWM disabled.
 * @pre Actuator output is zero; call once on core 0 before pump calibration.
 * @note An implausible zero remains visible as invalid telemetry.
 */
void VP37_currentSenseInit(void);

/**
 * @brief Start the hardware-paced scan of the shunt, sensor mux and supply.
 * @return HAL_OK, or the scan start status (HAL_EBUSY, HAL_ENOMEM, ...).
 * @note Single owner, core 1: the completion interrupt and the reducer share
 * that core. While the scan runs, on-demand reads of the three pins return
 * the newest scanned sample, so the sensor readers need no change.
 */
hal_status_t VP37_currentScanStart(void);

/** @brief Stop the scan; idempotent. */
hal_status_t VP37_currentScanStop(void);

/** @brief Frame period reported by the scan, 0 while it is not running. */
uint32_t VP37_currentScanFrameNs(void);

/**
 * @brief Reduce a contiguous scan history to current and supply observations.
 * @param block Non-NULL block view with valid positions and frame period.
 * @param out Non-NULL result, cleared first; zero state always filled.
 * @return HAL_OK, HAL_EINVAL (bad view), HAL_ESTATE (invalid zero),
 * HAL_EAGAIN (no complete rise-to-rise period or too few guarded samples),
 * or HAL_EOVERFLOW (ON phase longer than the sample budget).
 * @note The gate is recovered from the shunt waveform itself: the freewheel
 * path bypasses the source shunt, so the ON phase is the only non-zero span.
 * supplyVolts and supplyValid describe the same period and are filled on
 * every return that found a period; read waveformValid before any amperes.
 * supplyLatestVolts instead spans one period ending at the history's last
 * frame. It uses the measured period when plausible, otherwise the nominal
 * PWM period, and remains usable without valid current edges or shunt zero.
 * Its validity is independent of the return status.
 */
hal_status_t VP37_currentScanReduce(const VP37CurrentScanBlock *block,
                                    VP37CurrentPulseResult *out);

/**
 * @brief Take the newest block, retain continuous history and reduce it.
 * @param out Non-NULL result; untouched when no block was available.
 * @param sequence Non-NULL; block sequence when one was taken, else 0.
 * @return The take status (HAL_EAGAIN, HAL_ESTATE) when no block was taken,
 * otherwise the reduce status.
 * @note Core 1 only; a block is held for one block period, so call at least
 * once per block period to see every block. Start, stop, missed blocks or
 * discontinuous completion timestamps discard the retained history.
 */
hal_status_t VP37_currentScanCollect(VP37CurrentPulseResult *out,
                                     uint32_t *sequence);

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

/**
 * @brief Convert source-shunt amperes to a zero-corrected 12-bit ADC code.
 * @param amps Current in amperes, clamped to the 12-bit range.
 * @return ADC code, 0..4095, rounded to nearest.
 */
uint16_t VP37_currentAmpsToRaw(float amps);

#ifdef __cplusplus
}
#endif
#endif
