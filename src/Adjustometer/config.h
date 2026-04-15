#ifndef T_CONFIG
#define T_CONFIG

#include "hardwareConfig.h"

#define WATCHDOG_TIME 4000
#define UNSYNCHRONIZE_TIME 15
#define CORE_OPERATION_DELAY 1

// I2C slave register map.
#define ADJUSTOMETER_I2C_ADDR       0x57
#define ADJUSTOMETER_REG_PULSE_HI   0x00  // int16_t big-endian (2 bytes)
#define ADJUSTOMETER_REG_PULSE_LO   0x01
#define ADJUSTOMETER_REG_VOLTAGE    0x02  // uint8_t: supply voltage (scaled)
#define ADJUSTOMETER_REG_FUEL_TEMP  0x03  // uint8_t: fuel temperature
#define ADJUSTOMETER_REG_STATUS     0x04  // uint8_t: status bitmask (see sensors.h)

// Baseline lock uses adaptive tracking and must converge quickly.
#define ADJUSTOMETER_BASELINE_MIN_TIME_MS 80U
#define ADJUSTOMETER_BASELINE_MAX_TIME_MS 250U

// Convergence criteria for locking baseline.
#define ADJUSTOMETER_BASELINE_TRACK_SHIFT 2U
#define ADJUSTOMETER_BASELINE_LOCK_TOLERANCE_HZ 12U
#define ADJUSTOMETER_BASELINE_LOCK_WINDOWS 6U

// Near-zero spike suppression with hysteresis.
#define ADJUSTOMETER_ZERO_HOLD_ENTER_HZ 20
#define ADJUSTOMETER_ZERO_HOLD_EXIT_HZ 25
#define ADJUSTOMETER_ZERO_HOLD_RELEASE_WINDOWS 2U

// ADC EMA filter: alpha = 1/(2^SHIFT).  SHIFT=3 → 12.5% new, 87.5% old.
// Fuel temp and voltage change slowly, so heavy smoothing is fine.
#define ADC_EMA_SHIFT 3U

// Thermal compensation: fallback coefficient used before adaptive tuning
// kicks in (ΔT < ADAPTIVE_COMP_MIN_DT_C).  Value is Hz/°C × 10.
// Set to 0 because the drift sign varies between engine sessions —
// a positive fallback is destructive when the real drift is negative.
#define THERMAL_COMP_HZ_PER_C_X10 0

// EMA smoothing of fuel temperature inside ISR (fixed-point ×256).
// Shift=8 → tau ≈ 256 windows ≈ 0.44 s at 37 kHz / 64-pulse window.
// Eliminates ±1 °C ADC quantisation jitter without lagging real trends.
#define THERMAL_COMP_EMA_SHIFT 8U

// Adaptive thermal compensation.
// After ΔT ≥ MIN_DT °C from baseline, the ISR computes the actual Hz/°C
// drift rate from (filteredHz − baseline) / ΔT and uses that instead of
// the fixed THERMAL_COMP_HZ_PER_C_X10 fallback.
// MIN_DT=4 °C: compromise between seed accuracy and early activation.
// At 8 °C the threshold was never reached in short runs (ΔT≤7 °C).
// At 4 °C, nc ≈ rawDrift*2560/1024 is a reasonable first estimate.
#define ADAPTIVE_COMP_MIN_DT_C         4U
#define ADAPTIVE_COMP_MIN_DT_X256      (ADAPTIVE_COMP_MIN_DT_C * 256U)
#define ADAPTIVE_COMP_EMA_SHIFT        6U    // τ ≈ 64 windows ≈ 0.11 s
#define ADAPTIVE_COMP_MIN_COEFF_X10    5     // 0.5 Hz/°C floor
#define ADAPTIVE_COMP_MAX_COEFF_X10    200   // 20.0 Hz/°C ceiling

#endif
