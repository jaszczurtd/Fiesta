
#include "sensors.h"
#include "../common/fiesta_sensor_helpers.h"
#include "hal/core/hal_compiler.h"
#include <hal/i2c/hal_i2c_slave.h>
#include <math.h>
#include <stdlib.h>

// Signed right-shift must be arithmetic (sign-extending) for EMA filters to
// work correctly. GCC guarantees this; the assertion guards against
// non-conforming toolchains.
#ifdef __cplusplus
static_assert((-1 >> 1) == -1,
              "Arithmetic right-shift required for signed integers");
#else
_Static_assert((-1 >> 1) == -1,
               "Arithmetic right-shift required for signed integers");
#endif

static void processAdjustometerFrequency(uint32_t rawHz, uint32_t nowUs);
static bool captureStarted;
static uint32_t captureRetryUs;
static bool captureHealthy;
static uint32_t captureTicks[4];
static uint8_t captureIndex, captureFilled;
static const hal_pulse_capture_config_t captureConfig = {
    PIO_INTERRUPT_HALL, true, ADJUSTOMETER_SIGNAL_LOSS_MIN_US};
static void resetSensorsState(void);
static bool isSignalLost(void);

/**
 * @brief Initialize the I2C slave interface and default status register.
 * @return None.
 */
void initI2C(void) {
  hal_i2c_slave_init(PIN_SDA, PIN_SCL, ADJUSTOMETER_I2C_ADDR);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_STATUS,
                           ADJ_STATUS_BASELINE_PENDING);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_EXT_VERSION,
                           ADJUSTOMETER_EXT_VERSION);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_EXT_SEQ_BEGIN, 0U);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_EXT_FLAGS, 0U);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_EXT_SEQ_END, 0U);
}

/**
 * @brief Configure GPIO and ADC resources required by the Adjustometer.
 * @return None.
 */
void initBasicPIO(void) {
  hal_gpio_set_mode(PIO_INTERRUPT_HALL, HAL_GPIO_INPUT_PULLUP);
  hal_adc_set_resolution(HAL_ADC_UTIL_DEFAULT_BITS);
}

/**
 * @brief Reset sensor state and start hardware period capture.
 * @return None.
 */
void initSensors(void) {
  resetSensorsState();
  initBasicPIO();

  (void)hal_pulse_capture_deinit();
  captureStarted = hal_pulse_capture_init(&captureConfig) == HAL_OK;
  captureRetryUs = hal_micros();
}

static volatile int32_t adjustometerPulse = 0;
static volatile uint32_t adjustometerLastEdgeUs = 0;
static volatile uint32_t adjustometerSignalHz = 0;
static uint32_t adjustometerRawHz = 0;
static uint32_t adjustometerMeasuredUs = 0;
static uint32_t adjustometerSampleSequence = 0;
static uint32_t auxiliaryTelemetry = 0;
// Processing state belongs to Core0.
static uint32_t adjustometerFilteredHz = 0;
static uint32_t adjustometerBaselineStartUs = 0;
static uint32_t adjustometerBaselineEstimate = 0;
static uint32_t adjustometerBaselineStableWindows = 0;
static uint32_t adjustometerBaseline = 0;
// Cross-core: written by Core0, read by getters on both cores.
static volatile bool adjustometerBaselineReady = false;
// Post-convergence verification state.
static bool adjustometerVerifying = false;
static uint32_t adjustometerVerifyStartUs = 0;
static bool adjustometerZeroHold = true;
static int8_t adjustometerZeroCandidateSign = 0;
static uint8_t adjustometerZeroCandidateWindows = 0;

#if ADJUSTOMETER_SLIDING_WINDOW
static int32_t slidingEmaFraction;
#endif

// ADC EMA filter state for fuel-temp and supply-voltage readings.
// Used by Core1 only - no atomics needed.
static float filteredFuelTemp = -1.0f;
static float filteredVoltage = -1.0f;

// JaszczurHAL defines SECOND in milliseconds, so convert to microseconds for Hz
// = us_per_second / period_us.
#define US_PER_SECOND (SECOND * 1000UL)
// Number of pulses to accumulate before computing frequency.
// At 37 kHz this gives ~3.5 ms window, lowering output latency.
#define ADJUSTOMETER_PULSE_WINDOW 128U
#define ADJUSTOMETER_SIGNAL_LOSS_US 200000U
// EMA filter: new sample weight 1/4, following each 128-pulse window.
#define ADJUSTOMETER_EMA_SHIFT 2U
#define ADJUSTOMETER_BASELINE_MIN_TIME_US                                      \
  (ADJUSTOMETER_BASELINE_MIN_TIME_MS * 1000UL)
#define ADJUSTOMETER_BASELINE_MAX_TIME_US                                      \
  (ADJUSTOMETER_BASELINE_MAX_TIME_MS * 1000UL)
#define ADJUSTOMETER_BASELINE_VERIFY_US                                        \
  (ADJUSTOMETER_BASELINE_VERIFY_MS * 1000UL)

/**
 * @brief Return the absolute value of a signed 32-bit integer.
 * @param value Value to normalize.
 * @return Absolute value of @p value.
 */
static inline int32_t absI32(int32_t value) {
  return (value < 0) ? -value : value;
}

/**
 * @brief Apply the integer EMA used for Adjustometer frequency filtering.
 * @param rawHz Newly measured frequency.
 * @param filteredHz Previous filtered frequency.
 * @return Updated filtered frequency.
 */
static inline uint32_t applyAdjustometerEma(uint32_t rawHz,
                                            uint32_t filteredHz) {
  if (filteredHz == 0U) {
    return rawHz;
  }

#if ADJUSTOMETER_SLIDING_WINDOW
  if (adjustometerBaselineReady) {
    // 71/1024 approximates 1-(3/4)^(1/4); retain fractional hertz.
    const int64_t previous = (int64_t)filteredHz * 65536 + slidingEmaFraction;
    const int64_t next =
        previous + (((int64_t)rawHz * 65536 - previous) * 71) / 1024;
    const uint32_t rounded = (uint32_t)((next + 32768) / 65536);
    slidingEmaFraction = (int32_t)(next - (int64_t)rounded * 65536);
    return rounded;
  }
#endif

  // EMA: filtered += (rawHz - filtered) / (2^SHIFT)
  // Guarantee minimum ±1 step when delta != 0 to prevent integer truncation
  // stall (positive delta < 2^SHIFT would otherwise truncate to 0).
  int32_t delta = (int32_t)rawHz - (int32_t)filteredHz;
  int32_t step = delta >> ADJUSTOMETER_EMA_SHIFT;
  if (step == 0 && delta != 0) {
    step = (delta > 0) ? 1 : -1;
  }
  return filteredHz + (uint32_t)step;
}

/**
 * @brief Compute absolute difference between two unsigned 32-bit values.
 * @param a First value.
 * @param b Second value.
 * @return Absolute difference between @p a and @p b.
 */
static inline uint32_t absDiffU32(uint32_t a, uint32_t b) {
  return (a >= b) ? (a - b) : (b - a);
}

/** @brief Update filtering, baseline and zero hysteresis from a complete
 * window. */
static void processAdjustometerFrequency(uint32_t rawHz, uint32_t nowUs) {
  HAL_ATOMIC_FETCH_ADD(&adjustometerSampleSequence, 1U, HAL_ATOMIC_ACQ_REL);
  HAL_ATOMIC_STORE(&captureHealthy, true, HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&adjustometerRawHz, rawHz, HAL_ATOMIC_RELAXED);
  HAL_ATOMIC_STORE(&adjustometerMeasuredUs, nowUs, HAL_ATOMIC_RELAXED);
  uint32_t filtered = applyAdjustometerEma(rawHz, adjustometerFilteredHz);
  adjustometerFilteredHz = filtered;
  HAL_ATOMIC_STORE(&adjustometerSignalHz, filtered, HAL_ATOMIC_RELEASE);

  if (!HAL_ATOMIC_LOAD(&adjustometerBaselineReady, HAL_ATOMIC_ACQUIRE)) {
    if (!adjustometerVerifying) {
      // Phase 1: Convergence tracking
      if (adjustometerBaselineStartUs == 0U) {
        adjustometerBaselineStartUs = nowUs;
        adjustometerBaselineEstimate = filtered;
        adjustometerBaselineStableWindows = 0U;
      } else {
        adjustometerBaselineEstimate =
            adjustometerBaselineEstimate +
            (((int32_t)filtered - (int32_t)adjustometerBaselineEstimate) >>
             ADJUSTOMETER_BASELINE_TRACK_SHIFT);

        if (absDiffU32(filtered, adjustometerBaselineEstimate) <=
            ADJUSTOMETER_BASELINE_LOCK_TOLERANCE_HZ) {
          adjustometerBaselineStableWindows++;
        } else {
          adjustometerBaselineStableWindows = 0U;
        }
      }

      const uint32_t baselineElapsedUs = nowUs - adjustometerBaselineStartUs;
      const bool minTimeReached =
          (baselineElapsedUs >= ADJUSTOMETER_BASELINE_MIN_TIME_US);
      const bool maxTimeReached =
          (baselineElapsedUs >= ADJUSTOMETER_BASELINE_MAX_TIME_US);
      const bool baselineConverged =
          minTimeReached && (adjustometerBaselineStableWindows >=
                             ADJUSTOMETER_BASELINE_LOCK_WINDOWS);

      if (baselineConverged || maxTimeReached) {
        // Convergence succeeded - enter verification phase
        HAL_ATOMIC_STORE(&adjustometerBaseline, adjustometerBaselineEstimate,
                         HAL_ATOMIC_RELEASE);
        adjustometerFilteredHz = adjustometerBaselineEstimate;
        HAL_ATOMIC_STORE(&adjustometerSignalHz, adjustometerFilteredHz,
                         HAL_ATOMIC_RELEASE);
        adjustometerVerifying = true;
        adjustometerVerifyStartUs = nowUs;
      }
    } else {
      // Phase 2: Post-convergence verification
      // Detect slow oscillator drift invisible to the fast convergence
      // window.
      const uint32_t currentBaseline =
          HAL_ATOMIC_LOAD(&adjustometerBaseline, HAL_ATOMIC_ACQUIRE);
      const uint32_t drift = absDiffU32(filtered, currentBaseline);
      if (drift > ADJUSTOMETER_BASELINE_VERIFY_DRIFT_HZ) {
        // Oscillator still settling - restart convergence from scratch
        adjustometerVerifying = false;
        adjustometerBaselineStartUs = nowUs;
        adjustometerBaselineEstimate = filtered;
        adjustometerFilteredHz = filtered;
        adjustometerBaselineStableWindows = 0U;
      } else if ((nowUs - adjustometerVerifyStartUs) >=
                 ADJUSTOMETER_BASELINE_VERIFY_US) {
        // Verification passed - finalise baseline
        HAL_ATOMIC_STORE(&adjustometerBaseline, adjustometerBaselineEstimate,
                         HAL_ATOMIC_RELEASE);
        adjustometerFilteredHz = adjustometerBaselineEstimate;
        HAL_ATOMIC_STORE(&adjustometerSignalHz, adjustometerFilteredHz,
                         HAL_ATOMIC_RELEASE);
        adjustometerPulse = 0;
        adjustometerZeroHold = true;
        adjustometerZeroCandidateSign = 0;
        adjustometerZeroCandidateWindows = 0U;
        HAL_ATOMIC_STORE(&adjustometerBaselineReady, true, HAL_ATOMIC_RELEASE);
      } else {
        // Still verifying - keep EMA-tracking so final baseline is accurate
        adjustometerBaselineEstimate =
            adjustometerBaselineEstimate +
            (((int32_t)filtered - (int32_t)adjustometerBaselineEstimate) >>
             ADJUSTOMETER_BASELINE_TRACK_SHIFT);
      }
    }
    HAL_ATOMIC_STORE(&adjustometerPulse, (int32_t)0, HAL_ATOMIC_RELEASE);
  } else {
    const uint32_t currentBaseline =
        HAL_ATOMIC_LOAD(&adjustometerBaseline, HAL_ATOMIC_ACQUIRE);
    int32_t pulse = (int32_t)filtered - (int32_t)currentBaseline;

    const int32_t absPulse = absI32(pulse);

    if (absPulse <= (int32_t)ADJUSTOMETER_ZERO_HOLD_ENTER_HZ) {
      pulse = 0;
      adjustometerZeroHold = true;
      adjustometerZeroCandidateSign = 0;
      adjustometerZeroCandidateWindows = 0U;
    } else if (adjustometerZeroHold) {
      if (absPulse < (int32_t)ADJUSTOMETER_ZERO_HOLD_EXIT_HZ) {
        pulse = 0;
        adjustometerZeroCandidateSign = 0;
        adjustometerZeroCandidateWindows = 0U;
      } else {
        const int8_t pulseSign = (pulse > 0) ? 1 : -1;
        if (pulseSign == adjustometerZeroCandidateSign) {
          if (adjustometerZeroCandidateWindows < 255U) {
            adjustometerZeroCandidateWindows++;
          }
        } else {
          adjustometerZeroCandidateSign = pulseSign;
          adjustometerZeroCandidateWindows = 1U;
        }

        if (adjustometerZeroCandidateWindows <
            ADJUSTOMETER_ZERO_HOLD_RELEASE_WINDOWS *
                (ADJUSTOMETER_SLIDING_WINDOW ? 4U : 1U)) {
          pulse = 0;
        } else {
          adjustometerZeroHold = false;
          adjustometerZeroCandidateSign = 0;
          adjustometerZeroCandidateWindows = 0U;
        }
      }
    }

    HAL_ATOMIC_STORE(&adjustometerPulse, pulse, HAL_ATOMIC_RELEASE);
  }
  HAL_ATOMIC_FETCH_ADD(&adjustometerSampleSequence, 1U, HAL_ATOMIC_RELEASE);
}

/** @brief Discard an incomplete window after capture loss. */
static void discardCaptureWindow(void) {
  captureIndex = 0U;
  captureFilled = 0U;
  HAL_ATOMIC_STORE(&captureHealthy, false, HAL_ATOMIC_RELEASE);
  if (!HAL_ATOMIC_LOAD(&adjustometerBaselineReady, HAL_ATOMIC_ACQUIRE)) {
    adjustometerBaselineStartUs = 0U;
    adjustometerBaselineStableWindows = 0U;
    adjustometerVerifying = false;
  }
}

void updateAdjustometerCapture(void) {
  if (!captureStarted) {
    const uint32_t nowUs = hal_micros();
    if (!hal_elapsed_u32(nowUs, captureRetryUs, 100000U))
      return;
    captureRetryUs = nowUs;
    captureStarted = hal_pulse_capture_deinit() == HAL_OK &&
                     hal_pulse_capture_init(&captureConfig) == HAL_OK;
    return;
  }
  for (unsigned int block = 0U; block < 64U; ++block) {
    hal_pulse_capture_sample_t sample;
    const hal_status_t status = hal_pulse_capture_read(&sample);
    if (status == HAL_EAGAIN)
      return;
    if (status != HAL_OK) {
      discardCaptureWindow();
      if (status == HAL_ETIMEOUT)
        return;
      (void)hal_pulse_capture_deinit();
      captureStarted = false;
      captureRetryUs = hal_micros();
      return;
    }
    HAL_ATOMIC_STORE(&adjustometerLastEdgeUs, sample.measured_us,
                     HAL_ATOMIC_RELEASE);
    captureTicks[captureIndex] = sample.ticks;
    captureIndex = (uint8_t)((captureIndex + 1U) % COUNTOF(captureTicks));
    if (captureFilled < COUNTOF(captureTicks))
      ++captureFilled;
    if (captureFilled < COUNTOF(captureTicks))
      continue;
    uint64_t ticks = 0U;
    for (size_t i = 0U; i < COUNTOF(captureTicks); ++i)
      ticks += captureTicks[i];
    const uint32_t rawHz =
        (uint32_t)(((uint64_t)ADJUSTOMETER_PULSE_WINDOW * sample.clock_hz +
                    ticks / 2U) /
                   ticks);
    processAdjustometerFrequency(rawHz, sample.measured_us);
    if (!ADJUSTOMETER_SLIDING_WINDOW || !adjustometerBaselineReady) {
      captureIndex = 0U;
      captureFilled = 0U;
    }
  }
}

/**
 * @brief Check whether the Hall signal has timed out.
 * @return True when no pulse edge has been seen within the allowed timeout.
 */
static bool isSignalLost(void) {
  const uint32_t lastEdgeUs =
      HAL_ATOMIC_LOAD(&adjustometerLastEdgeUs, HAL_ATOMIC_ACQUIRE);
  if (!HAL_ATOMIC_LOAD(&captureHealthy, HAL_ATOMIC_ACQUIRE)) {
    return true;
  }

  const uint32_t nowUs = hal_micros();
  uint32_t signalLossUs = ADJUSTOMETER_SIGNAL_LOSS_US;
  const uint32_t signalHz =
      HAL_ATOMIC_LOAD(&adjustometerSignalHz, HAL_ATOMIC_ACQUIRE);
  if (signalHz > 0U) {
    const uint32_t periodUs =
        (uint32_t)((US_PER_SECOND + (signalHz / 2U)) / signalHz);
    uint32_t dynamicLossUs = periodUs * ADJUSTOMETER_SIGNAL_LOSS_MULTIPLIER;
    if (dynamicLossUs < ADJUSTOMETER_SIGNAL_LOSS_MIN_US) {
      dynamicLossUs = ADJUSTOMETER_SIGNAL_LOSS_MIN_US;
    } else if (dynamicLossUs > ADJUSTOMETER_SIGNAL_LOSS_MAX_US) {
      dynamicLossUs = ADJUSTOMETER_SIGNAL_LOSS_MAX_US;
    }
    signalLossUs = dynamicLossUs;
  }

  return (nowUs - lastEdgeUs) > signalLossUs;
}

/**
 * @brief Return the current Adjustometer pulse magnitude.
 * @return Current pulse value, or 0 while signal is lost.
 * @note This is a project-local G149-like raw quantity-feedback signal, not an
 * OEM quantity estimate and not a calibrated mg/stroke value.
 */
int32_t getAdjustometerPulses(void) {
  if (isSignalLost()) {
    return 0;
  }
  return abs(HAL_ATOMIC_LOAD(&adjustometerPulse, HAL_ATOMIC_ACQUIRE));
}

/**
 * @brief Return the current filtered oscillator frequency.
 * @return Signal frequency in hertz.
 * @note This is the oscillator-side raw observable behind the project's
 * G149-like quantity-feedback path.
 */
uint32_t getAdjustometerSignalHz(void) {
  return HAL_ATOMIC_LOAD(&adjustometerSignalHz, HAL_ATOMIC_ACQUIRE);
}

int32_t getAdjustometerSignedDeltaHz(void) {
  const uint32_t signalHz =
      HAL_ATOMIC_LOAD(&adjustometerSignalHz, HAL_ATOMIC_ACQUIRE);
  const uint32_t baselineHz =
      HAL_ATOMIC_LOAD(&adjustometerBaseline, HAL_ATOMIC_ACQUIRE);
  return (int32_t)signalHz - (int32_t)baselineHz;
}

/**
 * @brief Build the module status bitmask from current signal and ADC state.
 * @return Packed Adjustometer status flags.
 */
uint8_t getAdjustometerStatus(void) {
  uint8_t status = ADJ_STATUS_OK;

  if (isSignalLost()) {
    status |= ADJ_STATUS_SIGNAL_LOST;
  }
  if (!HAL_ATOMIC_LOAD(&adjustometerBaselineReady, HAL_ATOMIC_ACQUIRE)) {
    status |= ADJ_STATUS_BASELINE_PENDING;
  }
  status |=
      (uint8_t)(HAL_ATOMIC_LOAD(&auxiliaryTelemetry, HAL_ATOMIC_ACQUIRE) >> 16);

  return status;
}

/**
 * @brief Check whether baseline acquisition and verification are complete.
 * @return True when the Adjustometer is ready for ECU use.
 * @note The ECU treats this as readiness of the project-local G149-like path
 * before enabling its N146/G149-like inner loop.
 */
bool isAdjustometerReady(void) {
  return HAL_ATOMIC_LOAD(&adjustometerBaselineReady, HAL_ATOMIC_ACQUIRE);
}

/**
 * @brief Return the locked baseline frequency used as the zero point.
 * @return Baseline frequency in hertz.
 * @note This is the zero reference for the project-local G149-like
 * quantity-feedback path.
 */
uint32_t getBaseline(void) {
  return HAL_ATOMIC_LOAD(&adjustometerBaseline, HAL_ATOMIC_ACQUIRE);
}

/**
 * @brief Reset all runtime sensor, baseline and filter state.
 * @return None.
 */
static void resetSensorsState(void) {
  adjustometerRawHz = 0;
  adjustometerMeasuredUs = 0;
  adjustometerSampleSequence = 0;
  auxiliaryTelemetry =
      (uint32_t)(ADJ_STATUS_FUEL_TEMP_BROKEN | ADJ_STATUS_VOLTAGE_BAD) << 16;
  adjustometerPulse = 0;
  adjustometerLastEdgeUs = 0;
  adjustometerSignalHz = 0;
  captureIndex = captureFilled = 0U;
  HAL_ATOMIC_STORE(&captureHealthy, false, HAL_ATOMIC_RELEASE);
  adjustometerFilteredHz = 0;
  adjustometerBaselineStartUs = 0;
  adjustometerBaselineEstimate = 0;
  adjustometerBaselineStableWindows = 0;
  HAL_ATOMIC_STORE(&adjustometerBaseline, 0U, HAL_ATOMIC_RELEASE);
  HAL_ATOMIC_STORE(&adjustometerBaselineReady, false, HAL_ATOMIC_RELEASE);
  adjustometerVerifying = false;
  adjustometerVerifyStartUs = 0;
  adjustometerZeroHold = true;
  adjustometerZeroCandidateSign = 0;
  adjustometerZeroCandidateWindows = 0;
#if ADJUSTOMETER_SLIDING_WINDOW
  slidingEmaFraction = 0;
#endif
  filteredFuelTemp = -1.0f;
  filteredVoltage = -1.0f;
}

/**
 * @brief Apply the ADC-side floating-point EMA filter.
 * @param raw Latest ADC-derived value.
 * @param prev Previous filtered value.
 * @return Updated filtered value.
 */
static float adcEma(float raw, float prev) {
  if (prev < 0.0f) {
    return raw;
  }
  return prev + ((raw - prev) / (float)(1U << ADC_EMA_SHIFT));
}

/**
 * @brief Read and filter the module supply voltage.
 * @return Tenths-of-volt value clamped to an 8-bit register.
 */
uint8_t getSupplyVoltageRaw(void) {
  float avgAdc = 0.0f;
  float volts = 0.0f;
  if (fiesta_adc_read_average_ex(ADC_VOLT_PIN, &avgAdc) != HAL_OK ||
      fiesta_adc_to_voltage_ex((int)(avgAdc + 0.5f), (float)VDIV_R1_KOHM,
                               (float)VDIV_R2_KOHM, &volts) != HAL_OK) {
    volts = 0.0f;
  }
  filteredVoltage = adcEma(volts, filteredVoltage);
  if (isnan(filteredVoltage) || filteredVoltage < 0.0f)
    filteredVoltage = 0.0f;
  // Return tenths-of-volt clamped to uint8_t (0 = 0.0 V, 255 = 25.5 V).
  float tv = filteredVoltage * 10.0f + 0.5f;
  if (tv > 255.0f)
    tv = 255.0f;
  return (uint8_t)tv;
}

/**
 * @brief Read and filter the fuel temperature sensor.
 * @return Rounded fuel temperature in degrees Celsius as an 8-bit value.
 * @note This is the module's G81-like fuel-temperature input.
 */
uint8_t getFuelTemperatureRaw(void) {
  float tempC = 0.0f;
  if (fiesta_ntc_read_temperature_ex(ADC_FUEL_TEMP_PIN, R_VP37_FUEL_A,
                                     R_VP37_FUEL_B, &tempC) != HAL_OK) {
    tempC = 0.0f;
  }
  if (isnan(tempC))
    tempC = 0.0f;
  filteredFuelTemp = adcEma(tempC, filteredFuelTemp);
  if (isnan(filteredFuelTemp) || filteredFuelTemp < 0.0f)
    filteredFuelTemp = 0.0f;
  if (filteredFuelTemp > 255.0f)
    filteredFuelTemp = 255.0f;
  return (uint8_t)(filteredFuelTemp + 0.5f);
}

void updateAuxiliarySensors(void) {
  const uint8_t voltage = getSupplyVoltageRaw();
  const uint8_t fuelTemp = getFuelTemperatureRaw();
  uint8_t status = 0U;
  if (fuelTemp == ADJ_FUEL_TEMP_SENSOR_BROKEN) {
    status |= ADJ_STATUS_FUEL_TEMP_BROKEN;
  }
  if (voltage < ADJ_VOLTAGE_MIN_TV || voltage > ADJ_VOLTAGE_MAX_TV) {
    status |= ADJ_STATUS_VOLTAGE_BAD;
  }
  const uint32_t packed =
      voltage | ((uint32_t)fuelTemp << 8) | ((uint32_t)status << 16);
  HAL_ATOMIC_STORE(&auxiliaryTelemetry, packed, HAL_ATOMIC_RELEASE);
}

hal_status_t getAdjustometerFeedback(adjustometer_feedback_t *out) {
  if (out == NULL) {
    return HAL_EINVAL;
  }
  for (unsigned int attempt = 0U; attempt < 3U; attempt++) {
    const uint32_t before =
        HAL_ATOMIC_LOAD(&adjustometerSampleSequence, HAL_ATOMIC_ACQUIRE);
    if ((before & 1U) != 0U) {
      continue;
    }
    adjustometer_feedback_t sample;
    sample.rawHz = HAL_ATOMIC_LOAD(&adjustometerRawHz, HAL_ATOMIC_RELAXED);
    sample.filteredHz =
        HAL_ATOMIC_LOAD(&adjustometerSignalHz, HAL_ATOMIC_RELAXED);
    sample.baselineHz =
        HAL_ATOMIC_LOAD(&adjustometerBaseline, HAL_ATOMIC_RELAXED);
    sample.measuredUs =
        HAL_ATOMIC_LOAD(&adjustometerMeasuredUs, HAL_ATOMIC_RELAXED);
    sample.number = before >> 1;
    sample.pulseHz =
        (int16_t)hal_constrain(getAdjustometerPulses(), 0, INT16_MAX);
    sample.status = getAdjustometerStatus() &
                    (ADJ_STATUS_SIGNAL_LOST | ADJ_STATUS_BASELINE_PENDING);
    const uint32_t auxiliary =
        HAL_ATOMIC_LOAD(&auxiliaryTelemetry, HAL_ATOMIC_ACQUIRE);
    sample.status |= (uint8_t)(auxiliary >> 16);
    sample.voltage = (uint8_t)auxiliary;
    sample.fuelTemp = (uint8_t)(auxiliary >> 8);
    HAL_ATOMIC_THREAD_FENCE(HAL_ATOMIC_SEQ_CST);
    if (before !=
        HAL_ATOMIC_LOAD(&adjustometerSampleSequence, HAL_ATOMIC_ACQUIRE)) {
      continue;
    }
    const uint32_t ageUs = hal_micros() - sample.measuredUs;
    sample.ageUs = (uint16_t)(ageUs > UINT16_MAX ? UINT16_MAX : ageUs);
    *out = sample;
    return HAL_OK;
  }
  return HAL_EAGAIN;
}
