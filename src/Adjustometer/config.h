#pragma once

#include "../common/adjustometer_protocol.h"
#include "hardwareConfig.h"

#define WATCHDOG_TIME 4000
#define UNSYNCHRONIZE_TIME 15
#define CORE_OPERATION_DELAY 1
#define ADJUSTOMETER_EXT_UPDATE_MS 10U

#define DEBUG_DEEP 1

// Oscillator warm-up time after cold power-on [ms].
// The LC oscillator frequency can overshoot during the first few hundred
// milliseconds of operation.  Capturing the baseline during this transient
// causes inverted VP37 calibration values on the ECU.  This delay lets the
// oscillator settle before the Hall-sensor ISR starts counting pulses.
#define ADJUSTOMETER_WARMUP_MS 500U

// Baseline lock uses adaptive tracking and must converge quickly.
#define ADJUSTOMETER_BASELINE_MIN_TIME_MS 80U
#define ADJUSTOMETER_BASELINE_MAX_TIME_MS 250U

// Convergence criteria for locking baseline.
#define ADJUSTOMETER_BASELINE_TRACK_SHIFT 2U
#define ADJUSTOMETER_BASELINE_LOCK_TOLERANCE_HZ 12U
#define ADJUSTOMETER_BASELINE_LOCK_WINDOWS 6U

// Post-convergence verification: after baseline locks, continue monitoring
// for VERIFY_MS.  If the EMA-filtered frequency drifts more than
// VERIFY_DRIFT_HZ from the captured baseline, convergence restarts.
// This catches slow oscillator warm-up drift that the fast convergence
// window (±12 Hz over ~21 ms) cannot detect.
#define ADJUSTOMETER_BASELINE_VERIFY_MS 1000U
#define ADJUSTOMETER_BASELINE_VERIFY_DRIFT_HZ 500U

// Near-zero spike suppression with hysteresis.
// Enter threshold must be wide enough to suppress post-manipulation thermal/
// mechanical settling of the sensor (typically ~30-50 Hz residual that decays
// over minutes).  At full-scale ~8000 Hz the deadband is ~0.5 %.
#define ADJUSTOMETER_ZERO_HOLD_ENTER_HZ 40
#define ADJUSTOMETER_ZERO_HOLD_EXIT_HZ 50
#define ADJUSTOMETER_ZERO_HOLD_RELEASE_WINDOWS 2U

// ADC EMA filter: alpha = 1/(2^SHIFT).  SHIFT=3 -> 12.5% new, 87.5% old.
// Fuel temp and voltage change slowly, so heavy smoothing is fine.
#define ADC_EMA_SHIFT 3U
