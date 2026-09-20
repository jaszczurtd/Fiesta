#ifndef T_VP37_INTERNAL
#define T_VP37_INTERNAL

#include "ecu_unit_testing.h"
#include "vp37.h"

/* Shared calibration bounds for the supply scale and current reference. */
#define VP37_LOCAL_VOLTAGE_SCALE_MIN 0.8f
#define VP37_LOCAL_VOLTAGE_SCALE_MAX 1.2f

/* Shared between the translation units of the VP37 module and nothing else.
 * They all act on one VP37Pump: the control path runs on core 1 under
 * vp37StateMutex, the telemetry path on core 0 from a snapshot of the pump.
 * Nothing declared here crosses that boundary; a file split is not a thread
 * split.
 *
 *   vp37.c              lifecycle, demand, the control cycle
 *   vp37_feedback.c     Adjustometer position and calibration
 *   vp37_compensation.c supply and thermal multipliers, the shunt scan bridge
 *   vp37_control.c      feedforward, map trim, PID authority and hold
 *   vp37_telemetry.c    control sample, console lines, bench trace
 *   vp37_current.c      shunt capture and pulse analysis, no pump state
 *   vp37_current_control.c bounded current feedback and PWM command history
 */

#ifdef __cplusplus
extern "C" {
#endif

// ── vp37.c ──────────────────────────────────────────────────────────────────
bool VP37_demandAtRest(const VP37Pump *self);

// ── vp37_feedback.c ─────────────────────────────────────────────────────────
bool VP37_updateAdjustometerPosition(VP37Pump *self);
bool VP37_makeCalibration(VP37Pump *self);

// ── vp37_compensation.c ─────────────────────────────────────────────────────
void VP37_updateVoltageCorrection(VP37Pump *self, float dt);
void VP37_updateTemperatureCorrection(VP37Pump *self, float dt);
void VP37_updateDriveCorrection(VP37Pump *self, float dt);
void VP37_updateThermalScale(VP37Pump *self, float dt);

// ── vp37_current_control.c ─────────────────────────────────────────────────
/** @brief Clear current feedback/history while preserving its enable switch.
 * @param self Non-NULL pump owned by the control core. */
void VP37_resetCurrentControl(VP37Pump *self);
/** @brief Update bounded current correction from a matching past PWM period.
 * @param self Non-NULL pump owned by the control core.
 * @param dt Elapsed control time [s]; invalid/long steps clear feedback.
 * @note Call before computing position-PID output limits. Invalid observations
 * release correction with a bounded slope; rest clears it immediately. */
void VP37_updateCurrentControl(VP37Pump *self, float dt);
/** @brief Record the position command that the next PWM period may apply.
 * @param self Non-NULL pump owned by the control core.
 * @param nominalPWM FF+position PID in nominal PWM counts, before current trim.
 * @param deliveredPWM Actual duty after all multipliers and limits.
 * @param writtenUs Local timestamp immediately before writing PWM [us]. */
void VP37_recordCurrentCommand(VP37Pump *self, float nominalPWM,
                               int32_t deliveredPWM, uint32_t writtenUs);

// ── vp37_control.c ──────────────────────────────────────────────────────────
float VP37_feedForward(VP37Pump *self, int32_t position);
/**
 * @brief Follow the target's standing state with the blend filter.
 * @param self Non-NULL controller; pidDtUs supplies the elapsed time.
 * @param standingTarget True once the target has stood for
 * VP37_TARGET_STABLE_MS, whether or not the ramp has reached it.
 * @note The upper-stroke rules scale with the result: gain multiplier, error
 * bound and feedback lead. A moving target fades them out, so ramps and
 * cyclic demand keep the plain loop.
 */
void VP37_updateStandingBlend(VP37Pump *self, bool standingTarget);
/**
 * @brief Proportional gain for the demanded position.
 * @param self Non-NULL controller with a calibrated stroke.
 * @return Base gain times the stroke multiplier in force [PWM/Hz].
 */
float VP37_proportionalGain(const VP37Pump *self);
/**
 * @brief Position error as the loop may act on it at the demanded position.
 * @param self Non-NULL controller with a calibrated stroke.
 * @param error Position error [Hz].
 * @return The error, held inside the limit in force: the value of
 * VP37_PROPORTIONAL_ERROR_LIMIT_MAP for a standing target, its first row for a
 * moving one.
 * @note An approach from rest lags by several hundred hertz; with the whole lag
 * on the proportional term the actuator breaks over the holding-force steps
 * straight into the end stop. The bound works both ways: the swing that
 * follows reaches as far below the demand as above it, and a bound on one
 * side only left more ringing on the bench.
 */
float VP37_boundedError(const VP37Pump *self, float error);
/**
 * @brief Proportional action on the lag of the Adjustometer filter.
 * @param self Non-NULL controller; effectiveKp, error and feedback.leadHz are
 * read.
 * @return Nominal PWM to add to the correction; negative while the unfiltered
 * position runs ahead of the filtered one.
 * @note The PID keeps the filtered position, so the integral, its dead zone,
 * the settled hold and the telemetry see the quiet signal. Only the
 * proportional path is moved to the newest sample, inside the same bound.
 */
float VP37_feedbackLead(const VP37Pump *self);
float VP37_integralLimit(const VP37Pump *self);
float VP37_integralDeadband(const VP37Pump *self);
/**
 * @brief Apply the derivative gain for this target without clearing PID
 * history.
 * @param self Non-NULL controller with a calibrated stroke; pidDtUs supplies
 * the elapsed control time in microseconds.
 * @param targetSettled True when the target is stationary and its ramp has
 * ended.
 * @note Activation follows the settled state through a 50 ms low-pass filter,
 * so a renewed ramp fades the added damping without clearing its history.
 * The position weight rises across 85..90% of slewed demand. At or below 85%,
 * at rest, or with the addition disabled, the blend clears immediately and
 * only the configured base gain remains in force.
 */
void VP37_updateDerivativeGain(VP37Pump *self, bool targetSettled);
void VP37_updateIntegralHold(VP37Pump *self, bool targetSettled);
void VP37_transferIntegralToMapTrim(VP37Pump *self);

// ── vp37_telemetry.c ────────────────────────────────────────────────────────
#if ECU_FUNCTIONAL_TESTS_ENABLED
void VP37_traceRecord(const VP37Pump *self);
#endif

#ifdef __cplusplus
}
#endif

#endif
