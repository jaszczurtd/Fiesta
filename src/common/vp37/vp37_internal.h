#ifndef T_VP37_INTERNAL
#define T_VP37_INTERNAL

#include "../fiesta_unit_testing.h"
#include "vp37.h"
#include "vp37_tuning.h"

/* Shared calibration bounds for the supply scale and current reference. */
#define VP37_LOCAL_VOLTAGE_SCALE_MIN 0.8f
#define VP37_LOCAL_VOLTAGE_SCALE_MAX 1.2f

/* Shared between the translation units of the VP37 module and nothing else.
 * They all act on one VP37Pump: the control path runs on the control core and
 * owns the pump, the telemetry path on another core reads only the published
 * snapshot and the trace buffer. Nothing declared here crosses that boundary
 * except through vp37_snapshot.c; a file split is not a thread split.
 *
 *   vp37.c              lifecycle, demand, the control cycle
 *   vp37_snapshot.c     publication and lock-free copies of the pump
 *   vp37_adjustometer.c Adjustometer frames and the baseline wait
 *   vp37_feedback.c     Adjustometer position and calibration
 *   vp37_compensation.c supply and thermal multipliers, the shunt scan bridge
 *   vp37_control.c      feedforward, map trim, PID authority and hold
 *   vp37_tuning.c       bench-tunable gains, limits and switches
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

// ── vp37_adjustometer.c ─────────────────────────────────────────────────────
/** @brief Poll the Adjustometer until its baseline is ready, feeding the
 * watchdog; false after VP37_ADJUSTOMETER_BASELINE_WAIT_MS. */
bool VP37_waitForAdjustometerBaseline(VP37Pump *self);

// ── vp37_compensation.c ─────────────────────────────────────────────────────
/** @brief Copy a completed ADC block, even between position steps.
 * @param self Non-NULL pump owned by core 1.
 * @return Scan poll status; updates acquisition counters without reduction. */
hal_status_t VP37_acquireCurrentScan(VP37Pump *self);
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
/** @brief Match the latest healthy current capture to its latched command.
 * @param self Non-NULL pump owned by the control core.
 * @param nowUs Current local microsecond timestamp.
 * @return Matched command, or NULL for stale, invalid or ambiguous
 * observations.
 * @note The pointer remains valid until the next capture or history change. */
const VP37CurrentCommand *VP37_matchCurrentSample(const VP37Pump *self,
                                                  uint32_t nowUs);
/** @brief Check that the present drive still belongs to its quiet window.
 * @param self Non-NULL pump owned by the control core.
 * @param nowUs Current local microsecond timestamp.
 * @return True after a continuous quiet command, position and supply interval.
 */
bool VP37_driveIsSettled(const VP37Pump *self, uint32_t nowUs);
/** @brief Record the position command that the next PWM period may apply.
 * @param self Non-NULL pump owned by the control core.
 * @param nominalPWM FF+position PID in nominal PWM counts, before current trim.
 * @param deliveredPWM Actual duty after all multipliers and limits.
 * @param writtenUs Local timestamp immediately before writing PWM [us]. */
void VP37_recordCurrentCommand(VP37Pump *self, float nominalPWM,
                               int32_t deliveredPWM, uint32_t writtenUs);

// ── vp37_control.c ──────────────────────────────────────────────────────────
float VP37_feedForward(VP37Pump *self, int32_t position);
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

// ── vp37_snapshot.c ─────────────────────────────────────────────────────────
/** @brief Publish the pump; control core only, at the end of a step. */
void VP37_publish(VP37Pump *self);
/** @brief Publish a stop made outside the step, once; control core only. */
void VP37_publishStop(VP37Pump *self);
#if VP37_TELEMETRY_ENABLED
/** @brief The telemetry of the last publication as the control core built
 * it; control core only. */
const VP37Telemetry *VP37_publishedTelemetry(void);
#endif

// ── vp37_telemetry.c ────────────────────────────────────────────────────────
#if VP37_TELEMETRY_ENABLED
/** @brief Append the published step to a running trace; control core. */
void VP37_traceRecord(const VP37Pump *self);
/** @brief Complete a running trace without a sample; control core. */
void VP37_traceStop(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
