#ifndef T_VP37_INTERNAL
#define T_VP37_INTERNAL

#include "ecu_unit_testing.h"
#include "vp37.h"

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
 */

#ifdef __cplusplus
extern "C" {
#endif

// ── vp37_feedback.c ─────────────────────────────────────────────────────────
bool VP37_updateAdjustometerPosition(VP37Pump *self);
bool VP37_makeCalibration(VP37Pump *self);

// ── vp37_compensation.c ─────────────────────────────────────────────────────
void VP37_updateVoltageCorrection(VP37Pump *self, float dt);
void VP37_updateTemperatureCorrection(VP37Pump *self, float dt);
void VP37_updateDriveCorrection(VP37Pump *self, float dt);
void VP37_updateThermalScale(VP37Pump *self, float dt);

// ── vp37_control.c ──────────────────────────────────────────────────────────
float VP37_feedForward(VP37Pump *self, int32_t position);
float VP37_integralLimit(const VP37Pump *self);
float VP37_integralDeadband(const VP37Pump *self);
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
