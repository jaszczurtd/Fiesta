
#include "sensors.h"
#include <hal/hal_i2c_slave.h>

// Signed right-shift must be arithmetic (sign-extending) for EMA filters to work correctly.
// GCC guarantees this; the assertion guards against non-conforming toolchains.
#ifdef __cplusplus
static_assert((-1 >> 1) == -1, "Arithmetic right-shift required for signed integers");
#else
_Static_assert((-1 >> 1) == -1, "Arithmetic right-shift required for signed integers");
#endif

static void countAdjustometerPulses(void);
static void resetSensorsState(void);
static bool isSignalLost(void);

void initI2C(void) {
  hal_i2c_slave_init(PIN_SDA, PIN_SCL, ADJUSTOMETER_I2C_ADDR);
  hal_i2c_slave_reg_write8(ADJUSTOMETER_REG_STATUS, ADJ_STATUS_BASELINE_PENDING);
}

void initBasicPIO(void) {
  hal_gpio_set_mode(PIO_INTERRUPT_HALL, HAL_GPIO_INPUT_PULLUP);
  hal_adc_set_resolution(HAL_TOOLS_ADC_BITS);
}

void initSensors(void) {
  resetSensorsState();
  initBasicPIO();

  hal_gpio_attach_interrupt(PIO_INTERRUPT_HALL, countAdjustometerPulses, HAL_GPIO_IRQ_FALLING);

  // Elevate GPIO IRQ above I2C slave so Hall-sensor edges
  // are never blocked by I2C transactions — prevents pulse coalescence.
  hal_gpio_set_irq_priority(HAL_IRQ_PRIORITY_HIGHEST);
}

static volatile int32_t  adjustometerPulse = 0;
static volatile uint32_t adjustometerLastEdgeUs = 0;
static volatile uint32_t adjustometerSignalHz = 0;
// ISR-only state can stay non-volatile for tighter generated code.
static uint32_t adjustometerWindowStartUs = 0;
static uint32_t adjustometerWindowCount = 0;
static uint32_t adjustometerFilteredHz = 0;
static uint32_t adjustometerBaselineStartUs = 0;
static uint32_t adjustometerBaselineEstimate = 0;
static uint32_t adjustometerBaselineStableWindows = 0;
static uint32_t adjustometerBaseline = 0;
// Cross-core: written by ISR (Core0), read by getters (Core1).
static volatile bool     adjustometerBaselineReady = false;
// Post-convergence verification state.
static bool     adjustometerVerifying = false;
static uint32_t adjustometerVerifyStartUs = 0;
static bool     adjustometerZeroHold = true;
static int8_t   adjustometerZeroCandidateSign = 0;
static uint8_t  adjustometerZeroCandidateWindows = 0;

// ADC EMA filter state for fuel-temp and supply-voltage readings.
// Used by Core1 only — no atomics needed.
static float filteredFuelTemp = -1.0f;
static float filteredVoltage  = -1.0f;

// JaszczurHAL defines SECOND in milliseconds, so convert to microseconds for Hz = us_per_second / period_us.
#define US_PER_SECOND (SECOND * 1000UL)
// Number of pulses to accumulate before computing frequency.
// At 37 kHz this gives ~3.5 ms window, lowering output latency.
#define ADJUSTOMETER_PULSE_WINDOW 128U
#define ADJUSTOMETER_SIGNAL_LOSS_US 200000U
// EMA filter: weight = 1/(2^SHIFT). SHIFT=3 means new sample has 12.5% weight.
// With 128-pulse window this keeps jitter low while preserving fast step response.
#define ADJUSTOMETER_EMA_SHIFT 3U
#define ADJUSTOMETER_BASELINE_MIN_TIME_US (ADJUSTOMETER_BASELINE_MIN_TIME_MS * 1000UL)
#define ADJUSTOMETER_BASELINE_MAX_TIME_US (ADJUSTOMETER_BASELINE_MAX_TIME_MS * 1000UL)
#define ADJUSTOMETER_BASELINE_VERIFY_US  (ADJUSTOMETER_BASELINE_VERIFY_MS * 1000UL)

static inline int32_t absI32(int32_t value) {
  return (value < 0) ? -value : value;
}

static inline uint32_t applyAdjustometerEma(uint32_t rawHz, uint32_t filteredHz) {
  if (filteredHz == 0U) {
    return rawHz;
  }

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

static inline uint32_t absDiffU32(uint32_t a, uint32_t b) {
  return (a >= b) ? (a - b) : (b - a);
}

// ISR — intentionally does all frequency computation, baseline calibration,
// thermal compensation and zero-hold filtering in interrupt context.
// While best practice favours minimal ISRs, here the design is deliberate:
//  • Determinism — every 128-pulse window is processed immediately, without
//    jitter from loop scheduling or competing tasks.
//  • Responsiveness — the PID feedback value is always up-to-date the moment
//    Core1 reads it; no deferred work queue or flag polling.
//  • CPU budget — Core0 has no other duties; voltage and temperature ADC reads
//    are auxiliary, non-time-critical tasks handled on Core1, so the ISR can
//    safely use the full Core0 bandwidth.
static void countAdjustometerPulses(void) {
  const uint32_t nowUs = hal_micros();
  __atomic_store_n(&adjustometerLastEdgeUs, nowUs, __ATOMIC_RELEASE);

  if (adjustometerWindowStartUs == 0U) {
    adjustometerWindowStartUs = nowUs;
    adjustometerWindowCount = 0U;
  }

  adjustometerWindowCount++;

  if (adjustometerWindowCount >= ADJUSTOMETER_PULSE_WINDOW) {
    const uint32_t elapsedUs = nowUs - adjustometerWindowStartUs;
    if (elapsedUs > 0U) {
      const uint32_t rawHz = (uint32_t)(((uint64_t)adjustometerWindowCount * US_PER_SECOND + (elapsedUs / 2U)) / (uint64_t)elapsedUs);
      uint32_t filtered = applyAdjustometerEma(rawHz, adjustometerFilteredHz);
      adjustometerFilteredHz = filtered;
      __atomic_store_n(&adjustometerSignalHz, filtered, __ATOMIC_RELEASE);

      if (!adjustometerBaselineReady) {
        if (!adjustometerVerifying) {
          // Phase 1: Convergence tracking
          if (adjustometerBaselineStartUs == 0U) {
            adjustometerBaselineStartUs = nowUs;
            adjustometerBaselineEstimate = filtered;
            adjustometerBaselineStableWindows = 0U;
          } else {
            adjustometerBaselineEstimate = adjustometerBaselineEstimate +
                (((int32_t)filtered - (int32_t)adjustometerBaselineEstimate) >> ADJUSTOMETER_BASELINE_TRACK_SHIFT);

            if (absDiffU32(filtered, adjustometerBaselineEstimate) <= ADJUSTOMETER_BASELINE_LOCK_TOLERANCE_HZ) {
              adjustometerBaselineStableWindows++;
            } else {
              adjustometerBaselineStableWindows = 0U;
            }
          }

          const uint32_t baselineElapsedUs = nowUs - adjustometerBaselineStartUs;
          const bool minTimeReached = (baselineElapsedUs >= ADJUSTOMETER_BASELINE_MIN_TIME_US);
          const bool maxTimeReached = (baselineElapsedUs >= ADJUSTOMETER_BASELINE_MAX_TIME_US);
          const bool baselineConverged = minTimeReached &&
              (adjustometerBaselineStableWindows >= ADJUSTOMETER_BASELINE_LOCK_WINDOWS);

          if (baselineConverged || maxTimeReached) {
            // Convergence succeeded — enter verification phase
            adjustometerBaseline = adjustometerBaselineEstimate;
            adjustometerFilteredHz = adjustometerBaseline;
            __atomic_store_n(&adjustometerSignalHz, adjustometerFilteredHz, __ATOMIC_RELEASE);
            adjustometerVerifying = true;
            adjustometerVerifyStartUs = nowUs;
          }
        } else {
          // Phase 2: Post-convergence verification
          // Detect slow oscillator drift invisible to the fast convergence window.
          const uint32_t drift = absDiffU32(filtered, adjustometerBaseline);
          if (drift > ADJUSTOMETER_BASELINE_VERIFY_DRIFT_HZ) {
            // Oscillator still settling — restart convergence from scratch
            adjustometerVerifying = false;
            adjustometerBaselineStartUs = nowUs;
            adjustometerBaselineEstimate = filtered;
            adjustometerFilteredHz = filtered;
            adjustometerBaselineStableWindows = 0U;
          } else if ((nowUs - adjustometerVerifyStartUs) >= ADJUSTOMETER_BASELINE_VERIFY_US) {
            // Verification passed — finalise baseline
            adjustometerBaseline = adjustometerBaselineEstimate;
            adjustometerFilteredHz = adjustometerBaseline;
            __atomic_store_n(&adjustometerSignalHz, adjustometerFilteredHz, __ATOMIC_RELEASE);
            adjustometerPulse = 0;
            adjustometerZeroHold = true;
            adjustometerZeroCandidateSign = 0;
            adjustometerZeroCandidateWindows = 0U;
            __atomic_store_n(&adjustometerBaselineReady, true, __ATOMIC_RELEASE);
          } else {
            // Still verifying — keep EMA-tracking so final baseline is accurate
            adjustometerBaselineEstimate = adjustometerBaselineEstimate +
                (((int32_t)filtered - (int32_t)adjustometerBaselineEstimate) >> ADJUSTOMETER_BASELINE_TRACK_SHIFT);
          }
        }
        __atomic_store_n(&adjustometerPulse, (int32_t)0, __ATOMIC_RELEASE);
      } else {
        int32_t pulse = (int32_t)filtered - (int32_t)adjustometerBaseline;

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

            if (adjustometerZeroCandidateWindows < ADJUSTOMETER_ZERO_HOLD_RELEASE_WINDOWS) {
              pulse = 0;
            } else {
              adjustometerZeroHold = false;
              adjustometerZeroCandidateSign = 0;
              adjustometerZeroCandidateWindows = 0U;
            }
          }
        }

        __atomic_store_n(&adjustometerPulse, pulse, __ATOMIC_RELEASE);
      }
    }
    adjustometerWindowStartUs = nowUs;
    adjustometerWindowCount = 0U;
  }
}

// Returns true if signal loss is detected (no edges for too long).
static bool isSignalLost(void) {
  const uint32_t lastEdgeUs = __atomic_load_n(&adjustometerLastEdgeUs, __ATOMIC_ACQUIRE);
  if (lastEdgeUs == 0U) {
    return true;
  }

  const uint32_t nowUs = hal_micros();
  uint32_t signalLossUs = ADJUSTOMETER_SIGNAL_LOSS_US;
  const uint32_t signalHz = __atomic_load_n(&adjustometerSignalHz, __ATOMIC_ACQUIRE);
  if (signalHz > 0U) {
    const uint32_t periodUs = (uint32_t)((US_PER_SECOND + (signalHz / 2U)) / signalHz);
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

int32_t getAdjustometerPulses(void) {
  if (isSignalLost()) {
    return 0;
  }
  return abs(__atomic_load_n(&adjustometerPulse, __ATOMIC_ACQUIRE));
}

uint32_t getAdjustometerSignalHz(void) {
  return __atomic_load_n(&adjustometerSignalHz, __ATOMIC_ACQUIRE);
}

uint8_t getAdjustometerStatus(void) {
  uint8_t status = ADJ_STATUS_OK;

  if (isSignalLost()) {
    status |= ADJ_STATUS_SIGNAL_LOST;
  }
  if (!__atomic_load_n(&adjustometerBaselineReady, __ATOMIC_ACQUIRE)) {
    status |= ADJ_STATUS_BASELINE_PENDING;
  }
  // Fuel-temp sensor health: filteredFuelTemp is updated by getFuelTemperatureRaw()
  // on the same core (Core1) just before this function is called from
  // updateI2CRegisters().  Reading it directly is therefore safe and avoids the
  // need for a separate cross-core atomic.
  if ((uint8_t)(filteredFuelTemp + 0.5f) == ADJ_FUEL_TEMP_SENSOR_BROKEN) {
    status |= ADJ_STATUS_FUEL_TEMP_BROKEN;
  }
  {
    uint8_t v = getSupplyVoltageRaw();
    if (v < ADJ_VOLTAGE_MIN_TV || v > ADJ_VOLTAGE_MAX_TV) {
      status |= ADJ_STATUS_VOLTAGE_BAD;
    }
  }

  return status;
}

bool isAdjustometerReady(void) {
  return adjustometerBaselineReady;
}

uint32_t getBaseline(void) {
  return adjustometerBaseline;
}

static void resetSensorsState(void) {
  adjustometerPulse = 0;
  adjustometerLastEdgeUs = 0;
  adjustometerSignalHz = 0;
  adjustometerWindowStartUs = 0;
  adjustometerWindowCount = 0;
  adjustometerFilteredHz = 0;
  adjustometerBaselineStartUs = 0;
  adjustometerBaselineEstimate = 0;
  adjustometerBaselineStableWindows = 0;
  adjustometerBaseline = 0;
  adjustometerBaselineReady = false;
  adjustometerVerifying = false;
  adjustometerVerifyStartUs = 0;
  adjustometerZeroHold = true;
  adjustometerZeroCandidateSign = 0;
  adjustometerZeroCandidateWindows = 0;
  filteredFuelTemp = -1.0f;
  filteredVoltage = -1.0f;
}

static float adcEma(float raw, float prev) {
  if (prev < 0.0f) {
    return raw;
  }
  return prev + ((raw - prev) / (float)(1U << ADC_EMA_SHIFT));
}

uint8_t getSupplyVoltageRaw(void) {
  float avgAdc = getAverageValueFrom(ADC_VOLT_PIN);
  float volts = adcToVolt((int)(avgAdc + 0.5f),
                          (float)VDIV_R1_KOHM, (float)VDIV_R2_KOHM);
  filteredVoltage = adcEma(volts, filteredVoltage);
  if (isnan(filteredVoltage) || filteredVoltage < 0.0f) filteredVoltage = 0.0f;
  // Return tenths-of-volt clamped to uint8_t (0 = 0.0 V, 255 = 25.5 V).
  float tv = filteredVoltage * 10.0f + 0.5f;
  if (tv > 255.0f) tv = 255.0f;
  return (uint8_t)tv;
}

uint8_t getFuelTemperatureRaw(void) {
  float tempC = ntcToTemp(ADC_FUEL_TEMP_PIN, R_VP37_FUEL_A, R_VP37_FUEL_B);
  if (isnan(tempC)) tempC = 0.0f;
  filteredFuelTemp = adcEma(tempC, filteredFuelTemp);
  if (isnan(filteredFuelTemp) || filteredFuelTemp < 0.0f) filteredFuelTemp = 0.0f;
  if (filteredFuelTemp > 255.0f) filteredFuelTemp = 255.0f;
  return (uint8_t)(filteredFuelTemp + 0.5f);
}
