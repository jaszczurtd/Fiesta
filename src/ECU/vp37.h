#ifndef T_VP37
#define T_VP37

#include <JaszczurHAL.h>
#include <hal/control/hal_pid_controller.h>
#include <hal/serial/hal_serial.h>

#include "config.h"
#include "hardwareConfig.h"
#include "rpm.h"
#include "tests.h"
#include "turbo.h"
#include "vp37_current.h"

#include "engineMaps.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Source of the normal quantity demand.
 *
 * 1 selects the engine model, 0 the driver potentiometer read through the
 * HC4051 multiplexer. This is not a test setting: a functional test only
 * borrows the demand while it runs and hands it back to this source when it
 * stops, and the selection stays available with the tests compiled out.
 */
#ifndef VP37_ENGINE_OPERATION_MODE
#define VP37_ENGINE_OPERATION_MODE 0
#endif

/* Bench telemetry is dense enough to follow single control steps. */
#if ECU_FUNCTIONAL_TESTS_ENABLED
#define VP37_DEBUG_UPDATE 20U
#else
#define VP37_DEBUG_UPDATE 250U
#endif
#define VP37_TELEMETRY_UPDATE 500U
#define VP37_CYCLE_VOLTAGE_MAX_AGE_US 100000U

#define DEFAULT_INJECTION_PRESSURE 300 // bar

#define VP37_PID_TIME_UPDATE 5.0f // minimum control period [ms]
// PID + Feedforward (FF) architecture:
//   pwm = pwm_ff(desired) + pid_correction
// PID output is now interpreted as PWM correction (units: PWM counts),
// NOT as a position estimate. Gains are in PWM/Hz (Kp), PWM/(Hz*s) (Ki),
// PWM*s/Hz (Kd). Position feedback remains in Adjustometer Hz.
#define VP37_PID_KP 0.05f
#define VP37_PID_KI 0.2f
// Keep D disabled: bench comparisons have not confirmed better approaches.
#define VP37_PID_KD 0.0f
// Derivative filter time constant [s].
#define VP37_PID_TF 0.003f
// Preserve integral output authority when changing Ki (nominal PWM counts).
// Residual authority above the holding map, tapered near the upper endpoint.
#define VP37_PID_TRIM_PWM 120.0f
#define VP37_PID_TRIM_TOP_PWM 45.0f
#define VP37_PID_TRIM_TAPER_PERCENT 75.0f
#define VP37_BENCH_INTEGRAL_CAP_PWM VP37_PID_TRIM_PWM
#define VP37_PID_MAX_INTEGRAL (VP37_PID_TRIM_PWM / VP37_PID_KI)

// Continuous dead zone for integration only [Hz]. P and D remain active.
#define VP37_PID_DEADBAND 12
// Above VP37_PID_TRIM_TAPER_PERCENT the stroke loses position authority: the
// same command settles hundreds of hertz apart and the same current holds very
// different positions. Integration there walks until the mechanism breaks free
// and produces a roughly 1 Hz relaxation cycle, so the dead zone widens with
// position to a band that covers the insensitive range. Zero keeps the base
// dead zone across the whole stroke.
#define VP37_PID_DEADBAND_TOP_HZ 120.0f
#define VP37_PID_DEADBAND_TOP_MAX_HZ 600.0f
// After 100 ms continuously inside the narrow band, hold integral until a
// persistent error leaves the wider band. This avoids winding force against
// static friction and releasing it as a visible position jump. The bands stay
// fixed: scaling them with the integration dead zone let standing errors of
// twice the zone persist on the upper stroke, where the zone alone bounds them.
#define VP37_INTEGRAL_HOLD_ENTER_HZ 20
#define VP37_INTEGRAL_HOLD_CONFIRM_MS 100U
#define VP37_INTEGRAL_HOLD_EXIT_HZ 40
#define VP37_INTEGRAL_HOLD_RELEASE_MS 500U
// Every approach from rest starts with an empty integral, so the holding map's
// residual is uncovered until the integral rebuilds; on the near-zero-stiffness
// upper stroke that showed as a 400 Hz drop right after arrival. When the
// position settles, the integral moves into a per-position map trim that
// survives rest, and the next approach starts with the residual already in the
// feedforward. One knot per ten percent of travel, interpolated, bounded.
// Off by default: on this actuator the settled integral carries a friction
// share of the same size as the map residual, so consecutive approaches
// learned values of opposite sign. Bench command K1 enables it for trials.
#define VP37_MAP_TRIM_KNOTS 11U
#define VP37_MAP_TRIM_LIMIT_PWM 40.0f
// A one-percent potentiometer transition must persist before it changes the
// demand. Larger driver changes remain immediate.
#define VP37_POTENTIOMETER_STEP_CONFIRM_MS 150U
// Correction limits are calculated at the PWM reference temperature.
// The 22 C curve below defines their original electrical authority.
#define VP37_PID_CORR_LIMIT 220.0f
#define VP37_PID_CORR_LIMIT_NEGATIVE VP37_PID_CORR_LIMIT
#define VP37_PID_CORR_LIMIT_POSITIVE_COLD VP37_PID_CORR_LIMIT
#define VP37_PID_CORR_LIMIT_POSITIVE_MAX 340.0f

// Fuel temperature approximates coil temperature. The copper model is
// anchored at 22 C; the complete control command is normalized to 49 C.
#define VP37_THERMAL_REFERENCE_TEMP_C 22.0f
#define VP37_COPPER_TEMP_COEFFICIENT 0.00393f
#define VP37_THERMAL_TEMP_VALID_MAX_C 120.0f
// FF and PID use the warm bench reference; temperature scales their sum once.
#define VP37_PWM_REFERENCE_TEMP_C 49.0f
#define VP37_TEMPERATURE_FACTOR_MIN 0.8f
#define VP37_TEMPERATURE_FACTOR_MAX 1.2f
#define VP37_TEMPERATURE_FILTER_S 2.0f

// Drive-path resistance seen by the shunt capture at the feedforward map's
// reference temperature. Holding the actuator raises the coil resistance by
// several percent within minutes while the fuel sensor moves by about a degree,
// so the measured value replaces the fuel-temperature model whenever the
// capture is healthy. Bench derivation: 1.137 ohm at 26 C fuel, divided by the
// model factor 0.918 for that temperature.
#define VP37_DRIVE_REFERENCE_OHMS 1.24f
// A small command or a small current makes the ratio ill-conditioned.
#define VP37_DRIVE_MIN_CURRENT_A 1.5f
#define VP37_DRIVE_MIN_PWM 420
// Gate timing is polled, so the reconstruction only rejects gross mismatch.
#define VP37_DRIVE_PWM_MATCH_COUNTS 150
// The command must not have moved between the capture and its use.
#define VP37_DRIVE_COMMAND_MATCH_COUNTS 8
#define VP37_DRIVE_MAX_AGE_US 100000U
#define VP37_DRIVE_FILTER_S 2.0f
#define VP37_DRIVE_READY_SAMPLES 16U
// Resistance moves on a thermal time scale; a ready estimate keeps scaling
// the command through motion and only gives way once no capture has been
// accepted for this long.
#define VP37_DRIVE_STALE_MS 10000U
// Endpoints of the nonlinear positive-demand feedforward map [nominal PWM].
#define VP37_PWM_FF_AT_MIN 585
#define VP37_PWM_FF_AT_MAX 872
// Upper upward-motion correction at the reference rate [nominal PWM]: the
// scale of the map's motion column. The runtime boost below is applied as a
// ratio to this value.
#define VP37_PWM_FF_MOTION_BOOST 32.0f
// Runtime upward boost at start and after bench R; 40 = the map's column times
// 1.25 (bench A/B on 130 Hz, 2026-09-16: ascent medians -25 %, P95 unchanged).
#define VP37_PWM_FF_MOTION_BOOST_DEFAULT 40.0f
// Downward-motion correction at the reference rate [nominal PWM]: the command
// drops below the holding map while the target falls, so the return spring
// is not fighting a holding command that only the integral would unwind.
// Same filter and rate scale as the upward term. 30 halved the descent
// medians on 130 Hz; 20 and 40 were clearly worse, 50 and 80 overshoot.
// Bench commands U/J.
#define VP37_PWM_FF_DESCENT_BOOST 30.0f
// Bench ceiling for either motion boost [nominal PWM].
#define VP37_PWM_FF_MOTION_BOOST_MAX 200.0f
#define VP37_PWM_FF_MOTION_FILTER_S 0.01f
#define VP37_PWM_FF_MOTION_REFERENCE_RATE 125.0f
// Percent of calibrated travel per second; permits the existing cyclic ramp.
#define VP37_DESIRED_SLEW_PERCENT_PER_SECOND 300.0f
#define VP37_DESIRED_UPPER_SLEW_PERCENT_PER_SECOND 275.0f
#define VP37_DESIRED_UPPER_SLEW_START_PERCENT 75.0f
// An unchanged target uses a softer approach; moving ramps retain their rate.
#define VP37_TARGET_STABLE_MS 25U
#define VP37_STATIONARY_SLEW_PERCENT_PER_SECOND 150.0f
#define VP37_STATIONARY_UPPER_SLEW_PERCENT_PER_SECOND 125.0f
#define VP37_STATIONARY_MOTION_WEIGHT 0.3f

// calibration / stabilization values
#define PERCENTAGE_ERROR 3.0

#define VP37_OPERATION_DELAY 5 // microseconds

#define STABILITY_ADJUSTOMETER_TAB_SIZE 4
#define MIN_ADJUSTOMETER_VAL 10

#define VP37_CALIBRATION_MAX_PERCENTAGE 80
#define VP37_AVERAGE_VALUES_AMOUNT 5

#define VP37_PWM_MIN 378
#define VP37_PWM_MAX PWM_RESOLUTION

// Calibration samples are spaced in time and accepted only after a complete
// window is stable.  This lets a warm actuator take longer than the old fixed
// 200 ms delay without slowing a normally settling actuator unnecessarily.
#define VP37_CALIBRATION_SAMPLE_INTERVAL_MS 20
#define VP37_CALIBRATION_MIN_SETTLE_MS 200
#define VP37_CALIBRATION_TIMEOUT_MS 1000
#define VP37_CALIBRATION_STABLE_SAMPLES 6
#define VP37_CALIBRATION_STABLE_SPAN_HZ 40
#define VP37_CALIBRATION_MIN_TRAVEL_HZ 6000

// define this, to avoid magic numbers in the code
#define VP37_PERCENT_MIN 0
#define VP37_PERCENT_MAX 100

// Throttle range in percentage units.
// Adjusting this, we can limit the maximum throttle range available to the
// user. 100 means full range, 50 means half, etc.
#define VP37_ACCELERATION_MIN 0
#define VP37_ACCELERATION_MAX 100

// Ramp-down step per cycle (in throttle percentage units).
// Higher = faster descent. 0.5 = smooth, 5+ = snappy.
#define VP37_THROTTLE_RAMP_DOWN_STEP 2.9f
#define VP37_THROTTLE_RAMP_DOWN_INTERVAL_MS 20
// Hold the last PWM briefly on a failed transfer, without integrating stale
// data.
#define VP37_ADJ_COMM_CUTOFF_MS 20U

#define VP37_MIN_COMPENSATION_VOLTAGE 7.0f
// The command follows the rail through one short filter and nothing else.
// The old 0.5 V hysteresis with a one-second tracking window guarded against
// the 40 us local ADC snapshot landing in the ON or OFF phase of the actuator's
// own PWM; the full-period supply mean from the shunt capture has no such
// alias (bench: sd 0.011-0.017 V), and the rail itself moves 16 mOhm per
// ampere, so a direct 1/V scale closes a loop of gain 0.004. The full-period
// mean therefore scales the command as is, every capture; the filter applies
// only to the local snapshot fallback. Anything below 0.5 V used to be left
// to the integrator for seconds.
#define VP37_VOLTAGE_FILTER_S 0.05f
// If both voltage sources fail, choose the highest expected supply so the
// fallback cannot increase actuator drive.
#define VP37_MAX_EXPECTED_SUPPLY_VOLTAGE 15.0f

// Climb floor follows the slewed demand and releases above that demand.
// It must not inject the final target's feedforward ahead of the ramp.
#define VP37_PWM_FF_SOFT_FLOOR_MARGIN 80

#define TIMING_PWM_MIN 0
#define TIMING_PWM_MAX PWM_RESOLUTION

/**
 * @brief Reserved legacy hook for VP37-side fuel temperature sampling.
 * @return None.
 * @note When implemented, this would correspond to a G81-like fuel-temperature
 * input.
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
  bool currentObservationEnabled; /**< Scan publish switch (bench Q0/Q1). */
  bool cycleVoltageEnabled;       /**< Full-period supply mean from the shunt
                                     capture scales the command; the local ADC is
                                     the fallback. Bench switch V0/V1. */
  bool cycleVoltageUsed;
  bool cycleSupplyValid;
  float cycleSupplyVolts;
  uint32_t cycleSupplyUs;
  bool cycleCurrentValid;    /**< Reduced block passed every waveform rule. */
  float cycleCurrentAmps;    /**< ON-phase mean of the captured PWM period. */
  float cycleCurrentVolts;   /**< Supply that belongs to that same capture. */
  int32_t cycleCurrentPwm;   /**< Duty reconstructed from the measured gate. */
  int32_t cycleCurrentDrive; /**< Command that was live during the capture. */
  uint32_t cycleCurrentUs;
  VP37CurrentPulseResult cycleResult; /**< Newest reduced scan block, kept for
                                         the core-0 `VP37 IPULSE` report. */
  hal_status_t cycleResultStatus;
  uint32_t cycleResultSequence; /**< Increments once per reduced block. */
  uint32_t scanLastSequence;    /**< Scan block sequence last reduced. */
  uint32_t scanBlocks;          /**< Blocks reduced since start. */
  uint32_t scanGaps;            /**< Blocks the control loop never saw. */
  uint32_t scanFrameNs;         /**< Frame period reported by the scan. */
  bool scanRunning;
  float lastThrottle;
  int32_t potentiometerDemand;
  int32_t potentiometerCandidate;
  uint32_t potentiometerCandidateSinceMs;
  bool potentiometerDemandReady;
  bool calibrationDone;
  // Setpoint pipeline (current names -> functional meaning):
  //   desiredAdjustometerTarget : raw quantity-position target written by
  //   VP37_setVP37Throttle() desiredAdjustometer       : slew-rate limited
  //   quantity-position target actually fed to PID
  int32_t desiredAdjustometerTarget;
  int32_t desiredAdjustometer;
  int32_t currentAdjustometerPosition;
  int32_t pidErr;
  float pwmFeedForward;
  float feedForwardRiseBlend; /**< Filtered upward slew, normalized to the FF
                                 reference rate. */
  float feedForwardMotion;    /**< Additional nominal PWM for upward motion. */
  float pidCorrection;
  float pidPositiveLimit;
  float pwmValue;
  float voltageCorrection;
  float compensationInputVolts; /**< Fused voltage before hysteresis. */
  float compensationVolts;      /**< Held supply voltage used to scale PWM. */
  bool voltageReady;  /**< Compensation voltage has been initialized. */
  bool voltageFrozen; /**< Bench V2: hold the scale, open the supply loop. */
  int32_t lastPWMval;
  int32_t finalPWM;
  float lastVolts;
  float localVolts;         /**< Unfiltered local ECU ADC voltage. */
  float previousLocalVolts; /**< Previous local ADC sample. */
  float localVoltageScale;  /**< Slow local-to-Adjustometer calibration. */
  bool localVoltageReady;   /**< Local ADC source has a previous sample. */
  bool voltageOverRange;    /**< Local reading above the calibrated range;
                               it still scales the command down. */
  int adjustStabilityTable[STABILITY_ADJUSTOMETER_TAB_SIZE];
  int32_t VP37_ADJUST_MIN, VP37_ADJUST_MIDDLE, VP37_ADJUST_MAX,
      VP37_OPERATE_MAX;
  float pidTimeUpdate;
  float pidTf;
  uint32_t adjCommLostSince;
  uint32_t throttleRampLastMs;
  uint8_t lastAdjustometerStatus;
  bool pidSaturatedHigh;
  hal_pid_terms_t pidTerms;
  bool integralHold; /**< Settled-position hysteresis currently freezes I. */
  bool integralHoldEnterPending;
  uint32_t integralHoldEnterStartedMs;
  uint32_t integralHoldConfirmMs; /**< Continuous time inside the entry band. */
  bool integralHoldReleasePending;
  uint32_t integralHoldReleaseStartedMs;
  float pidNegativeLimit;
  float pidUpperLimit;
  float desiredPosition;
  uint32_t targetChangedMs;
  float pidKp, pidKi, pidKd;
  float pidIntegralLimit;    /**< Available integral contribution in nominal PWM
                                counts. */
  float pidIntegralOverride; /**< Bench cap in nominal PWM; zero selects
                                position profile. */
  float motionBoostUp;   /**< Upward motion correction at the reference rate,
                            nominal PWM; scales the map's motion column. */
  float motionBoostDown; /**< Downward motion correction at the reference
                            rate, nominal PWM, subtracted while the target
                            falls. */
  float feedForwardFallBlend;  /**< Filtered downward rate, reference units. */
  float integralDeadbandTopHz; /**< Dead zone at full stroke; zero keeps the
                                  base dead zone everywhere. */
  float integralDeadbandHz;    /**< Dead zone applied in the last step. */
  float mapTrim[VP37_MAP_TRIM_KNOTS]; /**< Learned holding-map residual per
                                         position knot, nominal PWM. */
  float mapTrimApplied;      /**< Trim added to the feedforward this step. */
  bool mapTrimEnabled;       /**< Learning and application switch. */
  bool integralHoldEntered;  /**< Hold engaged in this step; transfer once. */
  uint32_t mapTrimTransfers; /**< Integral-to-trim transfers so far. */
  float lastFuelTemp;
  float temperatureCorrection; /**< Filtered multiplier of the complete FF + PID
                                  command. */
  float temperatureCompensationWeight; /**< Bench blend: 0 disables, 1 applies
                                          the model. */
  bool temperatureReady;     /**< A valid temperature has initialized the
                                multiplier. */
  float driveResistance;     /**< Filtered drive-path resistance from the
                                measured current, in ohms. */
  float driveCorrection;     /**< Measured replacement for the fuel-temperature
                                multiplier; unity until enough samples. */
  uint32_t driveSamples;     /**< Accepted resistance observations. */
  uint32_t driveUpdatedMs;   /**< Last accepted observation. */
  bool driveResistanceReady; /**< Enough observations to drive the output. */
  bool driveCompensationEnabled; /**< Bench switch for the measured path. */
  bool driveCompensationUsed;    /**< Measured path scaled the last command. */
  uint32_t controlLastUs;
  uint32_t controlDtUs;
  uint32_t controlSequence;
  bool controlStarted;
  bool softFloorActive;
  bool pwmLimited;
  bool quantityAtRest; /**< Zero demand after slew: PWM off, PID state cleared.
                        */
  bool feedbackFresh;
  uint32_t feedbackRawHz, feedbackFilteredHz, feedbackNumber, feedbackUs;
  uint16_t feedbackAgeUs;
  hal_status_t feedbackReadStatus;
  uint32_t feedbackReadUs;
  uint8_t feedbackRetries;
  bool adjCommFailed;
  bool pidStarted;
  uint32_t pidLastUs, pidDtUs;
} VP37Pump;

/** @brief One control step; positions in Hz, elapsed time in us, output in PWM
 * counts. */
typedef struct {
  uint32_t us, dt,
      sequence;   /**< MCU timestamp, elapsed time and step number. */
  float throttle; /**< Requested percentage. */
  int32_t target, desired, measured,
      pwm;             /**< Target, slewed target, feedback and PWM. */
  float motionFF;      /**< Upward-motion component included in feedforward. */
  float ff, low, high; /**< Feedforward and effective correction limits. */
  float volts;         /**< Latest measured supply voltage (V). */
  float localVolts;    /**< Simultaneous local ECU ADC supply voltage (V). */
  float compensationInputVolts; /**< Fused voltage before hysteresis (V). */
  float compensationVolts;      /**< Supply voltage used for PWM scaling (V). */
  float voltageCorrection;      /**< Effective supply-voltage multiplier. */
  bool cycleVoltageUsed; /**< Selected a fresh complete PWM-period supply mean.
                          */
  bool voltageOverRange; /**< Supply reading above the calibrated range. */
  float mapTrim;     /**< Learned holding-map residual in the feedforward. */
  bool integralHold; /**< Settled-position integral hold is active. */
  float fuelTemp;    /**< Fuel temperature (C) used by control. */
  float temperatureCorrection; /**< Temperature multiplier used by this step. */
  hal_pid_terms_t terms; /**< Contributions and limits from the same step. */
  bool softFloor, hardwareClamp; /**< Active downstream bounds. */
  bool quantityAtRest;           /**< Quantity drive released at zero demand. */
  uint8_t status; /**< Adjustometer status from this control step. */
  uint32_t rawHz, filteredHz, sampleNumber, measuredUs;
  uint16_t ageUs;
  hal_status_t readStatus;
  uint32_t readUs;
  uint8_t retries;
  bool fresh;
  uint32_t cyclicDelayMs; /**< Active cyclic step delay, or zero otherwise. */
  uint32_t
      pidDtUs; /**< Elapsed time for a successful PID step; zero when held. */
} VP37TraceSample;

#if ECU_FUNCTIONAL_TESTS_ENABLED
/** @brief Steps in a bench RAM capture. Each step costs one VP37TraceSample
 * of static RAM, so this is the knob to turn when a build runs out. */
#ifndef VP37_TRACE_SAMPLES
#define VP37_TRACE_SAMPLES 1024U
#endif
/** @brief Start a capture under the owner mutex. Non-NULL self must be running.
 * @return HAL_OK, HAL_EINVAL for NULL, HAL_EBUSY if capturing/draining,
 * or HAL_EAGAIN when control is inactive. */
hal_status_t VP37_startTrace(VP37Pump *self);
/** @brief Read a completed capture under the owner mutex; both pointers
 * non-NULL. A stopped controller completes a partial capture. Error leaves
 * sample unchanged.
 * @return HAL_OK, HAL_EINVAL for NULL, HAL_EAGAIN while recording, or
 * HAL_ENOENT. */
hal_status_t VP37_readTrace(VP37Pump *self, VP37TraceSample *sample);
/** @brief Whether RAM recording is active; caller holds the owner mutex. */
bool VP37_traceCapturing(void);
/** @brief Print a non-NULL captured sample outside the owner mutex. */
void VP37_showTrace(const VP37TraceSample *sample);
#endif

typedef enum {
  VP37_INIT_OK = 0,
  VP37_INIT_ALREADY_INITIALIZED,
  VP37_INIT_BASELINE_NOT_READY,
  VP37_INIT_PID_CREATE_FAILED,
  VP37_INIT_CALIBRATION_FAILED
} VP37InitStatus;

/**
 * @brief Compute the available positive PID correction for a temperature.
 * @param fuelTempC Adjustometer fuel-temperature reading in degrees Celsius.
 * @param adjustometerStatus Latest Adjustometer status-bit field.
 * @param pwmFeedForward Nominal feedforward at the current requested position.
 * @return Positive PID correction limit in nominal-voltage PWM counts.
 * @note A broken or implausible temperature signal falls back to the original
 *       cold limit, so a sensor fault can never request extra actuator drive.
 */
float VP37_computePositiveCorrectionLimit(float fuelTempC,
                                          uint8_t adjustometerStatus,
                                          float pwmFeedForward);

/**
 * @brief Initialize the VP37 inner quantity-control loop and calibrate
 * Adjustometer limits.
 * @param self VP37 controller instance to initialize.
 * @return Initialization status code.
 * @note Functionally this brings up the project-local N146/G149-like path.
 *       Adjustometer remains only G149-like, not a literal OEM G149.
 */
VP37InitStatus VP37_init(VP37Pump *self);

/**
 * @brief Reduce the newest completed scan block and publish the observation.
 * @param self Non-NULL pump; the caller holds the pump mutex.
 * @return HAL_EAGAIN when no new block was available, otherwise the reduce
 * status; a rejected block still updates cycleResult for the report.
 * @note Core 1, once per control step. Publishing follows the observation
 * switch; the report fields are updated regardless of it.
 */
hal_status_t VP37_serviceCurrentScan(VP37Pump *self);

/**
 * @brief Print the `VP37 IPULSE` report for the newest reduced block.
 * @param self Non-NULL snapshot taken under the pump mutex.
 * @return None.
 */
void VP37_showCurrentPulse(const VP37Pump *self);

/**
 * @brief Process one cycle of the VP37 inner quantity-actuator loop.
 * @param self VP37 controller instance to process.
 * @return None.
 * @note This is the low-level N146/G149-like loop. Higher-level requested-fuel-
 *       quantity arbitration is still represented only partially in the current
 * code.
 */
void VP37_process(VP37Pump *self);

/**
 * @brief Enable or disable the VP37 output stage.
 * @param self VP37 controller instance issuing the command.
 * @param enable True to enable the actuator path, false to disable it.
 * @return None.
 * @note This is a project-local run/enable output and is only loosely
 * comparable to the OEM N109 stop-solenoid path.
 */
void VP37_enableVP37(VP37Pump *self, bool enable);

/** @brief Latch control off and remove PWM. Non-NULL self; restart requires
 * initialization. */
void VP37_stop(VP37Pump *self);

/**
 * @brief Read back the current VP37 enable output state.
 * @param self VP37 controller instance to inspect.
 * @return True when VP37 output is enabled, otherwise false.
 * @note The signal is project-local and should not be treated as a literal N109
 * alias.
 */
bool VP37_isVP37Enabled(VP37Pump *self);

/**
 * @brief Print VP37 controller state for diagnostics.
 * @param self Snapshot copied while holding the controller owner mutex.
 * @note Call outside that mutex; this function performs serial and I2C I/O.
 * @return None.
 */
void VP37_showDebug(VP37Pump *self);

/**
 * @brief Set the VP37 timing-actuator output as a normalized angle command.
 * @param self VP37 controller instance issuing the command.
 * @param angle Requested timing angle in the 0..100 range.
 * @return None.
 * @note In OEM terminology this is closest to commanding the N108
 * start-of-injection actuator path. Closed-loop G80/G28 SOI feedback is not
 * implemented here yet.
 */
void VP37_setInjectionTiming(VP37Pump *self, int32_t angle);

/**
 * @brief Convert legacy accelerator demand into a VP37 quantity-feedback
 * target.
 * @param self VP37 controller instance to update.
 * @param accel Accelerator / driver-demand input in percentage-like units.
 * @return None.
 * @note Despite the legacy "Throttle" name, this function currently maps
 * G79/G185-like driver demand directly into the project-local N146/G149-like
 * target.
 */
void VP37_setVP37Throttle(VP37Pump *self, float accel);

/**
 * @brief Apply the direct potentiometer demand with single-step debounce.
 * @param self VP37 controller instance to update.
 * @param accel Integer potentiometer demand in the 0..100 range.
 * @return None.
 * @note A one-percent change must persist for
 *       VP37_POTENTIOMETER_STEP_CONFIRM_MS; changes of at least two percent
 *       remain immediate.
 */
void VP37_setPotentiometerThrottle(VP37Pump *self, int32_t accel);

/**
 * @brief Update VP37 PID gains and optionally reset controller state.
 * @param self VP37 controller instance to update.
 * @param kp New proportional gain.
 * @param ki New integral gain.
 * @param kd New derivative gain.
 * @param shouldTriggerReset True to reset controller state after applying
 * gains.
 * @return None.
 */
void VP37_setVP37PID(VP37Pump *self, float kp, float ki, float kd,
                     bool shouldTriggerReset);

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
