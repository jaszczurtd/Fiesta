#ifndef T_VP37
#define T_VP37

#include <tools_c.h>
#include <hal/hal_pid_controller.h>
#include <hal/hal_serial.h>

#include "config.h"
#include "rpm.h"
#include "obd-2.h"
#include "turbo.h"
#include "hardwareConfig.h"
#include "tests.h"

#include "engineMaps.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VP37_DEBUG_UPDATE 500

#define DEFAULT_INJECTION_PRESSURE 300 //bar

#define VP37_PID_TIME_UPDATE 150.0
// PID + Feedforward (FF) architecture:
//   pwm = pwm_ff(desired) + pid_correction
// PID output is now interpreted as PWM correction (units: PWM counts),
// NOT as a position estimate. Gains are in PWM/Hz (Kp), PWM/(Hz*s) (Ki),
// PWM*s/Hz (Kd). Old position-tracking gains were ~4x larger because the
// adj->PWM linear map had ratio ~0.236 (1890 PWM / 8000 Hz).
#define VP37_PID_KP 0.08f
#define VP37_PID_KI 0.06f
#define VP37_PID_KD 0.015f
#define VP37_PID_TF 0.068f
#define VP37_PID_MAX_INTEGRAL 4096

// Error deadband [Hz]: errors smaller than this are treated as zero
// to prevent integral windup near the setpoint. ~0.6% of full range.
#define VP37_PID_DEADBAND 40
// Maximum PID correction magnitude in PWM units. PID adds at most
// +/- VP37_PID_CORR_LIMIT to the feedforward PWM. Keeps the controller
// from ramming the actuator into mechanical stops on transients.
#define VP37_PID_CORR_LIMIT 220
// Feedforward steady-state PWM at adjustometer MIN/MAX positions.
// Determined empirically from logs (steady-state PWM observed at idle vs.
// near full position). Linear interpolation between these points provides
// the bulk of the control signal; PID only trims the residual.
#define VP37_PWM_FF_AT_MIN 480
#define VP37_PWM_FF_AT_MAX 690
// Setpoint slew rate limit [Hz per PID cycle].
// Caps how fast desiredAdjustometer can change between PID updates.
// Prevents the FF term from issuing huge instantaneous PWM jumps when
// throttle changes rapidly. 400/cycle @ 150ms = ~2667 Hz/s = full range
// (8000 Hz) traversal in ~3 s.
// Asymmetric: down-slew is slower because mechanical inertia + return spring
// already drag the actuator back; matching FF too quickly causes the actuator
// to overshoot the new (lower) target by 500-1100 Hz on throttle release.
#define VP37_DESIRED_SLEW_PER_CYCLE 800
#define VP37_DESIRED_SLEW_PER_CYCLE_DOWN 500

// calibration / stabilization values
#define PERCENTAGE_ERROR 3.0

#define VP37_OPERATION_DELAY 5 //microseconds

#define STABILITY_ADJUSTOMETER_TAB_SIZE 4
#define MIN_ADJUSTOMETER_VAL 10

#define VP37_CALIBRATION_MAX_PERCENTAGE 80
#define VP37_AVERAGE_VALUES_AMOUNT 5

#define VP37_PWM_MIN 378
#define VP37_PWM_MAX (VP37_PWM_MIN * 6.0)

#define VP37_ADJUST_TIMER 200

//define this, to avoid magic numbers in the code
#define VP37_PERCENT_MIN 0
#define VP37_PERCENT_MAX 100

//Throttle range in percentage units. 
//Adjusting this, we can limit the maximum throttle range available to the user. 
//100 means full range, 50 means half, etc.
#define VP37_ACCELERATION_MIN 0
#define VP37_ACCELERATION_MAX 100

// Ramp-down step per cycle (in throttle percentage units).
// Higher = faster descent. 0.5 = smooth, 5+ = snappy.
#define VP37_THROTTLE_RAMP_DOWN_STEP 2.9f
#define VP37_THROTTLE_RAMP_DOWN_INTERVAL_MS 20
// Time in seconds with commOk==false before VP37 is disabled.
#define VP37_ADJ_COMM_CUTOFF_S 5

#define VP37_MIN_COMPENSATION_VOLTAGE 7.0f

// Soft floor on the commanded PWM (in nominal-voltage units), applied BEFORE
// voltage compensation. While the actuator is still travelling toward a
// higher target, the slewed setpoint (and thus FF) is well below the
// final target's FF. PID alone may not push hard enough on the way up,
// causing sluggish climb. The soft floor keeps pwmValue at least
// (FF_at_target - margin) so the coil always has enough drive to reach
// the target promptly. Margin allows PID to undershoot slightly when the
// adjustometer overshoots, without yanking PWM back to the FF curve.
#define VP37_PWM_FF_SOFT_FLOOR_MARGIN 80

#define TIMING_PWM_MIN 0
#define TIMING_PWM_MAX PWM_RESOLUTION

/**
 * @brief Reserved legacy hook for VP37-side fuel temperature sampling.
 * @return None.
 * @note When implemented, this would correspond to a G81-like fuel-temperature input.
 */
void measureFuelTemp(void);

/**
 * @brief Reserved legacy hook for VP37-side supply voltage sampling.
 * @return None.
 */
void measureVoltage(void);

typedef struct {
  hal_pid_controller_t adjustController;

  bool vp37Initialized;
  float lastThrottle;
  bool calibrationDone;
  // Setpoint pipeline (current names -> functional meaning):
  //   desiredAdjustometerTarget : raw quantity-position target written by VP37_setVP37Throttle()
  //   desiredAdjustometer       : slew-rate limited quantity-position target actually fed to PID
  int32_t desiredAdjustometerTarget;
  int32_t desiredAdjustometer;
  int32_t currentAdjustometerPosition;
  int32_t pidErr;
  float pwmValue;
  float voltageCorrection;
  int32_t lastPWMval;
  int32_t finalPWM;
  float lastVolts;
  int adjustStabilityTable[STABILITY_ADJUSTOMETER_TAB_SIZE];
  int32_t VP37_ADJUST_MIN, VP37_ADJUST_MIDDLE, VP37_ADJUST_MAX, VP37_OPERATE_MAX;
  float pidTimeUpdate;
  float pidTf;
  uint32_t adjCommLostSince;
  uint32_t throttleRampLastMs;
} VP37Pump;

typedef enum {
  VP37_INIT_OK = 0,
  VP37_INIT_ALREADY_INITIALIZED,
  VP37_INIT_BASELINE_NOT_READY,
  VP37_INIT_PID_CREATE_FAILED
} VP37InitStatus;

/**
 * @brief Initialize the VP37 inner quantity-control loop and calibrate Adjustometer limits.
 * @param self VP37 controller instance to initialize.
 * @return Initialization status code.
 * @note Functionally this brings up the project-local N146/G149-like path.
 *       Adjustometer remains only G149-like, not a literal OEM G149.
 */
VP37InitStatus VP37_init(VP37Pump *self);

/**
 * @brief Process one cycle of the VP37 inner quantity-actuator loop.
 * @param self VP37 controller instance to process.
 * @return None.
 * @note This is the low-level N146/G149-like loop. Higher-level requested-fuel-
 *       quantity arbitration is still represented only partially in the current code.
 */
void VP37_process(VP37Pump *self);

/**
 * @brief Enable or disable the VP37 output stage.
 * @param self VP37 controller instance issuing the command.
 * @param enable True to enable the actuator path, false to disable it.
 * @return None.
 * @note This is a project-local run/enable output and is only loosely comparable to
 *       the OEM N109 stop-solenoid path.
 */
void VP37_enableVP37(VP37Pump *self, bool enable);

/**
 * @brief Read back the current VP37 enable output state.
 * @param self VP37 controller instance to inspect.
 * @return True when VP37 output is enabled, otherwise false.
 * @note The signal is project-local and should not be treated as a literal N109 alias.
 */
bool VP37_isVP37Enabled(VP37Pump *self);

/**
 * @brief Print VP37 controller state for diagnostics.
 * @param self VP37 controller instance to report.
 * @return None.
 */
void VP37_showDebug(VP37Pump *self);

/**
 * @brief Set the VP37 timing-actuator output as a normalized angle command.
 * @param self VP37 controller instance issuing the command.
 * @param angle Requested timing angle in the 0..100 range.
 * @return None.
 * @note In OEM terminology this is closest to commanding the N108 start-of-injection
 *       actuator path. Closed-loop G80/G28 SOI feedback is not implemented here yet.
 */
void VP37_setInjectionTiming(VP37Pump *self, int32_t angle);

/**
 * @brief Convert legacy accelerator demand into a VP37 quantity-feedback target.
 * @param self VP37 controller instance to update.
 * @param accel Accelerator / driver-demand input in percentage-like units.
 * @return None.
 * @note Despite the legacy "Throttle" name, this function currently maps G79/G185-like
 *       driver demand directly into the project-local N146/G149-like target.
 */
void VP37_setVP37Throttle(VP37Pump *self, float accel);

/**
 * @brief Update VP37 PID gains and optionally reset controller state.
 * @param self VP37 controller instance to update.
 * @param kp New proportional gain.
 * @param ki New integral gain.
 * @param kd New derivative gain.
 * @param shouldTriggerReset True to reset controller state after applying gains.
 * @return None.
 */
void VP37_setVP37PID(VP37Pump *self, float kp, float ki, float kd, bool shouldTriggerReset);

/**
 * @brief Read back the current VP37 PID gains.
 * @param self VP37 controller instance to inspect.
 * @param kp Output pointer receiving proportional gain, or NULL.
 * @param ki Output pointer receiving integral gain, or NULL.
 * @param kd Output pointer receiving derivative gain, or NULL.
 * @return None.
 */
void VP37_getVP37PIDValues(VP37Pump *self, float *kp, float *ki, float *kd);

/**
 * @brief Get the current VP37 PID update interval.
 * @param self VP37 controller instance to inspect.
 * @return PID update time in milliseconds.
 */
float VP37_getVP37PIDTimeUpdate(VP37Pump *self);

#ifdef __cplusplus
}
#endif

#endif
