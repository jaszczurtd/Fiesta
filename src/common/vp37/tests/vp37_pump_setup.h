#pragma once
// A calibrated pump ready for VP37_process(), set up against whichever board
// services the including fixture provides. Shared by the module fixture and
// the ECU one, so the pump defaults exist once.
#include "../vp37_internal.h"
#include <string.h>

static inline void vp37SetupCalibratedPump(VP37Pump *pump,
                                           const VP37Callbacks *callbacks,
                                           bool fastFrame) {
  memset(pump, 0, sizeof(*pump));
  (void)VP37_setCallbacks(pump, callbacks);
  if (fastFrame) {
    (void)VP37_setAdjustometerFastFeedback(pump, true);
  }
  pump->pid.controller = hal_pid_controller_create();
  pump->vp37Initialized = true;
  pump->feedback.calibrationDone = true;
  pump->feedback.adjustMin = 100;
  pump->feedback.adjustMax = 9100;
  pump->feedback.adjustMiddle =
      (pump->feedback.adjustMax + pump->feedback.adjustMin) / 2;
  pump->demand.target = -1;
  pump->demand.desired = -1;
  pump->demand.requestedPercent = -1.0f;
  pump->demand.topArrivalDecel = VP37_TOP_ARRIVAL_DECEL_PERCENT_PER_S2;
  pump->feedforward.motionRateCap = VP37_PWM_FF_MOTION_RATE_CAP_PERCENT_PER_S;
  pump->pidTimeUpdate = VP37_PID_TIME_UPDATE;
  pump->pid.topKd = VP37_PID_TOP_KD;
  pump->pid.integralHoldConfirmMs = VP37_INTEGRAL_HOLD_CONFIRM_MS;
  pump->feedforward.motionBoostUp = VP37_PWM_FF_MOTION_BOOST;
  pump->feedforward.motionBoostDown = VP37_PWM_FF_DESCENT_BOOST;
  pump->thermal.temperatureCorrection = 1.0f;
  pump->thermal.temperatureCompensationWeight = 1.0f;
  pump->thermal.driveResistance = VP37_DRIVE_REFERENCE_OHMS;
  pump->thermal.driveCorrection = 1.0f;
  pump->supply.heldVolts = VP37_NOMINAL_VOLTAGE;
  pump->supply.ready = false;
  pump->feedback.lastStatus = ADJ_STATUS_OK;
  VP37_setVP37PID(pump, VP37_PID_KP, VP37_PID_KI, VP37_PID_KD, false);
  hal_pid_controller_set_tf(pump->pid.controller, VP37_PID_TF);
  hal_pid_controller_set_max_integral(pump->pid.controller,
                                      VP37_PID_MAX_INTEGRAL);
}
