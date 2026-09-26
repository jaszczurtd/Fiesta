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
 * @brief What zero demand does to the drive once its descent has finished.
 *
 * 1 releases the spring-return actuator: the PID and every feedforward term
 * are cleared and the PWM goes off, so nothing holds the quantity at zero and
 * no current flows at rest. 0 keeps the loop running at the calibrated bottom
 * of the stroke instead: the holding command and the correction stay live and
 * the actuator sits there under drive. With 0 the drive never rests, so the
 * local supply divider is never trained against the Adjustometer's reading
 * and the shunt capture serves the supply mean at zero demand too. Bench
 * builds set it through JH_EXTRA_DEFINES.
 */
#ifndef VP37_PWM_DISABLE_AT_MIN_POSITION
#define VP37_PWM_DISABLE_AT_MIN_POSITION 1
#endif

/* Bench telemetry is dense enough to follow single control steps. */
#if ECU_FUNCTIONAL_TESTS_ENABLED
#define VP37_DEBUG_UPDATE 20U
#else
#define VP37_DEBUG_UPDATE 250U
#endif
#define VP37_TELEMETRY_UPDATE 500U
/** Maximum age of the supply averaging window's midpoint [us]. */
#define VP37_CYCLE_VOLTAGE_MAX_AGE_US 20000U
/** Time constant of the supply slope used to predict PWM application [s]. */
#define VP37_VOLTAGE_SLOPE_FILTER_S 0.02f
/** Maximum voltage lead added to a fresh supply mean [V]. */
#define VP37_VOLTAGE_PREDICTION_LIMIT_V 0.5f

#define DEFAULT_INJECTION_PRESSURE 300 // bar

#define VP37_PID_TIME_UPDATE 5.0f // minimum control period [ms]
// PID + Feedforward (FF) architecture:
//   pwm = pwm_ff(desired) + pid_correction
// PID output is now interpreted as PWM correction (units: PWM counts),
// NOT as a position estimate. Gains are in PWM/Hz (Kp), PWM/(Hz*s) (Ki),
// PWM*s/Hz (Kd). Position feedback remains in Adjustometer Hz.
#define VP37_PID_KP 0.05f
#define VP37_PID_KI 0.2f
// Moving targets fade the upper-target D addition toward the base PI response.
#define VP37_PID_KD 0.0f
/** Additional derivative gain at settled upper targets [PWM*s/Hz]; zero
 * disables it. */
#define VP37_PID_TOP_KD 0.001f
/** Time constant for engaging and releasing upper-target damping [s]. */
#define VP37_PID_TOP_D_BLEND_S 0.05f
// Derivative filter time constant [s].
#define VP37_PID_TF 0.003f
// Column order of the stroke tapers in engineMaps.h: {demand [%], value}.
#define VP37_TAPER_COL_PERCENT 0U
#define VP37_TAPER_COL_VALUE 1U
// Preserve integral output authority when changing Ki (nominal PWM counts).
// The residual authority above the holding map and its taper near the upper
// endpoint are the VP37_INTEGRAL_LIMIT_MAP rows in engineMaps.h.
#define VP37_PID_TRIM_PWM (VP37_INTEGRAL_LIMIT_MAP[0U][VP37_TAPER_COL_VALUE])
#define VP37_BENCH_INTEGRAL_CAP_PWM VP37_PID_TRIM_PWM
#define VP37_PID_MAX_INTEGRAL (VP37_PID_TRIM_PWM / VP37_PID_KI)

// Continuous dead zone for integration only; P and D remain active. Above the
// taper start the stroke loses position authority: the same command settles
// hundreds of hertz apart and the same current holds very different
// positions. Integration there walks until the mechanism breaks free and
// produces a roughly 1 Hz relaxation cycle, so the dead zone widens with
// position to a band that covers the insensitive range; the rows are
// VP37_INTEGRAL_DEADBAND_MAP in engineMaps.h. A zero top keeps the base dead
// zone across the whole stroke; the bench may raise the top to this ceiling.
#define VP37_PID_DEADBAND (VP37_INTEGRAL_DEADBAND_MAP[0U][VP37_TAPER_COL_VALUE])
#define VP37_PID_DEADBAND_TOP_HZ                                               \
  (VP37_INTEGRAL_DEADBAND_MAP[VP37_STROKE_TAPER_KNOTS - 1U]                    \
                             [VP37_TAPER_COL_VALUE])
#define VP37_PID_DEADBAND_TOP_MAX_HZ 600.0f
// After 100 ms continuously inside the narrow band, hold integral until a
// persistent error leaves the wider band. This avoids winding force against
// static friction and releasing it as a visible position jump. The bands stay
// fixed: scaling them with the integration dead zone let standing errors of
// twice the zone persist on the upper stroke, where the zone alone bounds them.
// Entry equals the base dead zone: the braked arrival approaches from one side,
// and a 20 Hz entry froze it up to 20 Hz short of the target.
#define VP37_INTEGRAL_HOLD_ENTER_HZ 12
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
// capture is healthy. Measured at 130 Hz on 2026-09-16 by two routes that agree
// inside one percent: eight cold-coil holds across the stroke gave 1.108 ohm at
// 28 C fuel, which the copper model carries to 1.200 at the reference, and the
// map's own holds ran at 1.187. The 1.24 ohm it replaces came from the 200 Hz
// configuration, where the capture reconstructed a different duty fraction, and
// left the measured path four percent below the model it hands over to. The
// feedforward below carries the reciprocal of that correction, so the commands
// stay where the map measured them.
#define VP37_DRIVE_REFERENCE_OHMS 1.20f
/** Proportional current-feedback gain in nominal PWM per equivalent PWM. */
#define VP37_CURRENT_CONTROL_GAIN 0.35f
/** Maximum current correction in the shared nominal command domain. */
#define VP37_CURRENT_CONTROL_LIMIT_PWM 40.0f
/** Maximum correction engagement/release rate [nominal PWM/s]. */
#define VP37_CURRENT_CONTROL_SLEW_PWM_S 1000.0f
/** Maximum age of the measured ON-phase midpoint [us]. */
#define VP37_CURRENT_CONTROL_MAX_AGE_US 25000U
/** PWM writes near a measured wrap have ambiguous latch ownership. */
#define VP37_CURRENT_CONTROL_EDGE_GUARD_US 100U
/** Allow scan quantization when matching observed and delivered duty. */
#define VP37_CURRENT_CONTROL_DUTY_TOLERANCE 24
/** Covers delayed observations at the normal 5 ms control period. */
#define VP37_CURRENT_CONTROL_HISTORY 16U
// A small command or a small current makes the ratio ill-conditioned.
#define VP37_DRIVE_MIN_CURRENT_A 1.5f
#define VP37_DRIVE_MIN_PWM 420
// Resistance learning requires a quiet command and position before capture.
#define VP37_DRIVE_COMMAND_MATCH_COUNTS 8
/** Maximum accumulated position drift inside a learning window [Hz]. */
#define VP37_DRIVE_POSITION_WINDOW_HZ 60
/** Continuous quiet-drive interval required before a current capture [us]. */
#define VP37_DRIVE_STABLE_US 150000U
#define VP37_DRIVE_MAX_AGE_US 100000U
#define VP37_DRIVE_FILTER_S 2.0f
// A ready estimate keeps following matched captures taken in motion at the
// same rate. Bench 2026-09-23: during a cyclic sweep the observed resistance
// rides the current lag behind the duty (about +-0.7 % in step with the
// direction of motion), so a fast estimate doubles as motion feedforward.
// Freezing it (rev87), slowing it to 10 s (rev88) or capping it at
// 0.001 ohm/s (rev89) raised the cyclic tracking error in that order.
#define VP37_DRIVE_FILTER_MOTION_S 2.0f
/** Bound the estimator's filter step after missing observations [s]. */
#define VP37_DRIVE_FILTER_MAX_STEP_S 0.05f
/** Cumulative supply change that pauses resistance learning [V]. */
#define VP37_DRIVE_VOLTAGE_CHANGE_V 0.1f
/** Quiet time before resistance learning resumes after a supply change [ms]. */
#define VP37_DRIVE_VOLTAGE_SETTLE_MS 100U
#define VP37_DRIVE_READY_SAMPLES 16U
// The filter starts at the reference and walks toward what the path measures,
// so the estimate is only worth using once it has had a few time constants to
// get there. Bench 2026-09-16: without this the first captures of a cold, fast
// ramp were reconstructed at 1.6 ohm, the correction clamped at its ceiling and
// the actuator sat against the upper stop for six seconds.
#define VP37_DRIVE_SETTLE_MS 6000U
// Resistance moves on a thermal time scale; a ready estimate keeps scaling
// the command through motion and only gives way once no healthy observation
// has arrived for this long.
#define VP37_DRIVE_STALE_MS 10000U
// Ceiling on how fast the thermal multiplier may move, per second. The measured
// path and the model disagree by whatever the coil has self-heated, so handing
// over between them steps the command; bench 2026-09-16 turned a 0.037 handover
// into a 375 Hz excursion. Resistance drifts about 0.0002 per second, two
// decades below this limit, so the ramp only ever blunts a handover.
#define VP37_THERMAL_SCALE_SLEW_PER_S 0.02f
// Column order of the holding map in engineMaps.h: {demand [%], holding
// command, motion correction}. Its endpoints and the scale of its motion
// column are read from the table; the runtime boosts below are ratios and
// offsets against them.
#define VP37_FF_COL_PERCENT 0U
#define VP37_FF_COL_PWM 1U
#define VP37_FF_COL_MOTION 2U
#define VP37_PWM_FF_AT_MIN (VP37_FF_MAP[0U][VP37_FF_COL_PWM])
#define VP37_PWM_FF_AT_MAX (VP37_FF_MAP[VP37_FF_KNOTS - 1U][VP37_FF_COL_PWM])
#define VP37_PWM_FF_MOTION_BOOST                                               \
  (VP37_FF_MAP[VP37_FF_KNOTS - 1U][VP37_FF_COL_MOTION])
// Runtime upward boost at start and after bench R: the map's column times
// 1.25 (bench A/B on 130 Hz, 2026-09-16: ascent medians -25 %, P95 unchanged),
// carried through the same reference rescale as the map.
#define VP37_PWM_FF_MOTION_BOOST_DEFAULT 38.7f
/** Upward motion assistance fades across this demand interval [% of stroke]. */
#define VP37_PWM_FF_MOTION_TAPER_START 75.0f
/** At this demand and above, the holding map and PID provide upward drive. */
#define VP37_PWM_FF_MOTION_TAPER_END 85.0f
// Downward-motion correction at the reference rate [nominal PWM]: the command
// drops below the holding map while the target falls, so the return spring
// is not fighting a holding command that only the integral would unwind.
// Same filter and rate scale as the upward term. The tuned value halved the
// descent medians on 130 Hz; a fifth below and above was clearly worse, and
// well above that it overshoots. Rescaled with the map when the drive
// reference was re-measured. Bench commands U/J.
#define VP37_PWM_FF_DESCENT_BOOST 29.0f
// Bench ceiling for either motion boost [nominal PWM].
#define VP37_PWM_FF_MOTION_BOOST_MAX 193.0f
#define VP37_PWM_FF_MOTION_FILTER_S 0.01f
#define VP37_PWM_FF_MOTION_REFERENCE_RATE 125.0f
// Percent of calibrated travel per second; permits the existing cyclic ramp.
#define VP37_DESIRED_SLEW_PERCENT_PER_SECOND 300.0f
#define VP37_DESIRED_UPPER_SLEW_PERCENT_PER_SECOND 275.0f
#define VP37_DESIRED_UPPER_SLEW_START_PERCENT 75.0f
// A lone rising step, or a target that has not changed for
// VP37_TARGET_STABLE_MS, is standing and gets the softer approach; a target
// that changes again within that time is tracked and keeps the moving rate
// and motion assist. A lone falling step keeps the full motion assist for
// that time, as every step did before the standing rule: the descent is not
// braked, and without that start it lagged its ramp by 20-60 Hz more.
#define VP37_TARGET_STABLE_MS 25U
#define VP37_STATIONARY_SLEW_PERCENT_PER_SECOND 225.0f
#define VP37_STATIONARY_UPPER_SLEW_PERCENT_PER_SECOND 187.5f
// Share of the motion assist a standing ramp gets, rising and falling. The
// rising share follows the ramp closely enough that the actuator lags it by
// ~80-110 Hz RMS less than at 0.3 (bench lag-1, 2026-09-25) with the same
// arrival time; 1.0 adds 30-90 Hz of overshoot. The descent is not braked,
// so a larger falling share only overshoots below the target.
#define VP37_STATIONARY_RISE_WEIGHT 0.6f
#define VP37_STATIONARY_FALL_WEIGHT 0.3f
// A rising standing ramp brakes before its target at this rate [% of
// travel/s^2], so the actuator arrives slowly instead of overshooting and
// ringing at ~6-9 Hz; the undamped mid stroke has no D to stop it. 750 halved
// the ringing of 1500 on small steps; the faster standing slew above keeps big
// steps quicker than without the brake.
#define VP37_ARRIVAL_DECEL_PERCENT_PER_S2 750.0f

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
/** Physical top of the usable stroke [% of the calibrated travel]. Demand
 * 0..100 % maps onto 0..this share of the travel, so the upper stroke with its
 * negative-stiffness steps (from ~90 %, static sweep 2026-09-20) stays out of
 * reach; the maximum fuel quantity is cut accordingly. 100 restores the full
 * travel. The stroke maps (holding map, tapers, upper damping) keep the
 * physical scale. */
#define VP37_PHYSICAL_LIMIT_PERCENT 86.0f
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

// At a settled demand the climb floor includes negative learned integral trim.
// It follows the slewed demand and releases above that demand.
#define VP37_PWM_FF_SOFT_FLOOR_MARGIN 80

#define TIMING_PWM_MIN 0
#define TIMING_PWM_MAX PWM_RESOLUTION

/**
 * @brief Reserved legacy hook for VP37-side fuel temperature sampling.
 * @note When implemented, this would correspond to a G81-like fuel-temperature
 * input.
 */
void measureFuelTemp(void);

/**
 * @brief Reserved legacy hook for VP37-side supply voltage sampling.
 */
void measureVoltage(void);

/** @brief Adjustometer position feedback and the calibrated range. */
typedef struct {
  bool calibrationDone;
  int32_t position; /**< Newest quantity position, in adjustometer Hz. */
  int stabilityTable[STABILITY_ADJUSTOMETER_TAB_SIZE];
  int32_t adjustMin;      /**< Calibrated bottom of the stroke. */
  int32_t adjustMiddle;   /**< Calibrated middle of the stroke. */
  int32_t adjustMax;      /**< Calibrated top of the stroke. */
  int32_t operateMax;     /**< Highest position the controller may request. */
  uint32_t commLostSince; /**< hal_millis() when the adjustometer went quiet,
                             0 while it answers. */
  uint8_t lastStatus;     /**< Status byte of the last adjustometer frame. */
  bool fresh;             /**< A new sample arrived since the previous cycle. */
  uint32_t rawHz;         /**< Unfiltered position sample. */
  uint32_t filteredHz;    /**< Adjustometer-filtered position sample. */
  uint32_t sampleNumber;  /**< Sequence number of the newest sample. */
  uint32_t sampleUs;      /**< Adjustometer time stamp of the newest sample. */
  uint16_t ageUs;         /**< Age of the newest sample when the adjustometer
                               published it. */
  hal_status_t readStatus; /**< Result of the last adjustometer read. */
  uint32_t readUs;         /**< Duration of the last adjustometer read. */
  uint8_t retries;         /**< Retries the last adjustometer read needed. */
  bool commFailed;         /**< Adjustometer silent for longer than allowed. */
} VP37Feedback;

/** @brief Source-independent requested position and its shared motion ramp. */
typedef struct {
  float requestedPercent;     /**< Requested position in 0..100%; -1 before a
                                 demand. */
  float physicalLimitPercent; /**< Share of the travel that 100 % demand
                                 reaches; VP37_PHYSICAL_LIMIT_PERCENT at start.
                                 Outside (0, 100] the full travel applies. */
  int32_t target;             /**< Calibrated position target [Hz]. */
  int32_t desired;            /**< Ramped target sent to the PID [Hz]. */
  float desiredPosition;      /**< Fractional ramp state [Hz]. */
  uint32_t targetChangedMs;
  bool targetMoving; /**< The last target change came within
                        VP37_TARGET_STABLE_MS of the one before it. */
  bool atRest;       /**< Zero demand after slew: PWM off, PID state cleared. */
} VP37Demand;

/** @brief Holding command from the map, the motion terms and the learned
 * trim. */
typedef struct {
  float pwm;
  float riseBlend;       /**< Filtered upward slew, normalized to the FF
                                       reference rate. */
  float motion;          /**< Additional nominal PWM for upward motion. */
  float motionBoostUp;   /**< Upward motion correction at the reference rate,
                            nominal PWM; scales the map's motion column. */
  float motionBoostDown; /**< Downward motion correction at the reference
                            rate, nominal PWM, subtracted while the target
                            falls. */
  float fallBlend;       /**< Filtered downward rate, reference units. */
  float mapTrim[VP37_MAP_TRIM_KNOTS]; /**< Learned holding-map residual per
                                         position knot, nominal PWM. */
  float mapTrimApplied;      /**< Trim added to the feedforward this step. */
  bool mapTrimEnabled;       /**< Learning and application switch. */
  uint32_t mapTrimTransfers; /**< Integral-to-trim transfers so far. */
} VP37Feedforward;

/** @brief The correction loop: gains, authority, dead zone and the settled
 * hold. */
typedef struct {
  hal_pid_controller_t controller;
  int32_t error; /**< Position error the controller last saw, in Hz. */
  float correction;
  float positiveLimit;
  float tf;
  bool saturatedHigh;
  hal_pid_terms_t terms;
  bool integralHold; /**< Settled-position hysteresis currently freezes I. */
  bool integralHoldEnterPending;
  uint32_t integralHoldEnterStartedMs;
  uint32_t integralHoldConfirmMs; /**< Continuous time inside the entry band. */
  bool integralHoldReleasePending;
  uint32_t integralHoldReleaseStartedMs;
  float negativeLimit;
  float upperLimit;
  float kp, ki, kd;
  float topKd;         /**< Additional settled-target D gain [PWM*s/Hz], blended
                            from zero at 85% to its full value at 90% demand. */
  float effectiveKd;   /**< Base plus scheduled D gain last sent to the PID,
                            in PWM*s/Hz. */
  float topDBlend;     /**< Filtered upper-target damping activation, 0..1. */
  float integralLimit; /**< Available integral contribution in nominal PWM
                             counts. */
  float integralOverride;      /**< Bench cap in nominal PWM; zero selects
                                     position profile. */
  float integralDeadbandTopHz; /**< Dead zone at full stroke; zero keeps the
                                  base dead zone everywhere. */
  float integralDeadbandHz;    /**< Dead zone applied in the last step. */
  bool integralHoldEntered;    /**< Hold engaged in this step; transfer once. */
  bool softFloorActive;
} VP37Pid;

/** @brief Supply-voltage multiplier and the sources it is fused from. */
typedef struct {
  bool cycleEnabled; /**< Full-period supply mean from the shunt
                               capture scales the command; the local ADC is
                               the fallback. Bench switch V0/V1. */
  bool cycleUsed;
  bool cycleValid;
  float cycleVolts;
  uint32_t cycleUs; /**< Midpoint of the latest supply window [hal_micros]. */
  uint32_t cycleAgeUs; /**< Window age when used by the control step [us]. */
  uint32_t predictionSampleUs; /**< Last supply window consumed by the slope. */
  float predictionSampleVolts; /**< Calibrated mean of that window [V]. */
  float voltageSlope; /**< Filtered slope of fresh supply means [V/s]. */
  float
      predictionVolts; /**< Bounded lead added only to the voltage scale [V]. */
  bool predictionReady; /**< A previous supply window is available. */
  float correction;
  float inputVolts; /**< Measured, calibrated voltage before prediction. */
  float heldVolts;  /**< Predicted or fallback-filtered voltage used by PWM. */
  bool ready;       /**< Compensation voltage has been initialized. */
  bool frozen;      /**< Bench V2: hold the scale, open the supply loop. */
  float lastVolts;
  float localVolts;         /**< Unfiltered local ECU ADC voltage. */
  float previousLocalVolts; /**< Previous local ADC sample. */
  float localScale;         /**< Slow local-to-Adjustometer calibration. */
  bool localReady;          /**< Local ADC source has a previous sample. */
  bool overRange;           /**< Local reading above the calibrated range;
                                      it still scales the command down. */
} VP37Supply;

/** @brief Thermal multiplier: the fuel-temperature model, the measured drive
 * resistance, the handover between them and the current observation that
 * feeds it. */
typedef struct {
  bool observationEnabled; /**< Scan publish switch (bench Q0/Q1). */
  bool cycleValid;         /**< Healthy capture matched to a latched command. */
  bool cycleSettled;  /**< The matched command was settled before capture. */
  float cycleAmps;    /**< ON-phase mean of the captured PWM period. */
  float cycleVolts;   /**< Supply that belongs to that same capture. */
  int32_t cyclePwm;   /**< Duty reconstructed between actual PWM latches. */
  int32_t cycleDrive; /**< Historical command matched to the PWM latch. */
  uint32_t cycleUs;
  float lastFuelTemp;
  float temperatureCorrection; /**< Filtered multiplier of the complete FF + PID
                                  command. */
  float temperatureCompensationWeight; /**< Bench blend: 0 disables, 1 applies
                                          the model. */
  bool temperatureReady; /**< A valid temperature has initialized the
                            multiplier. */
  float driveResistance; /**< Filtered drive-path resistance from the
                            measured current, in ohms. */
  float
      driveObservationOhms; /**< Last valid matched ratio, including motion. */
  bool driveLearning;       /**< This step accepted a resistance observation. */
  uint32_t
      driveLearnedSamples; /**< Accepted updates, wrapping at UINT32_MAX. */
  float driveCorrection;   /**< Measured replacement for the fuel-temperature
                              multiplier; unity until enough samples. */
  uint32_t driveSamples;   /**< Accepted resistance observations. */
  uint32_t driveUpdatedMs; /**< Last observation that updated resistance. */
  uint32_t
      driveObservedMs;         /**< Last healthy new observation, for expiry;
                                  includes observations paused by a supply change. */
  uint32_t driveFirstSampleMs; /**< First observation of the current estimate;
                                  the filter needs time from here, not just
                                  a count. */
  uint32_t
      driveLastCycleUs; /**< Last consumed current observation timestamp. */
  bool driveCycleSeen;  /**< Timestamp zero is valid at timer wrap. */
  float driveVoltageReference; /**< Supply anchor for detecting a transition. */
  uint32_t driveVoltageChangedMs; /**< Last change of the supply anchor. */
  bool driveVoltageReady;         /**< Supply anchor has been initialized. */
  bool
      driveVoltageSettled; /**< Resistance learning may use the current rail. */
  bool driveResistanceReady; /**< Enough observations to drive the output. */
  bool driveCompensationEnabled; /**< Bench switch for the measured path. */
  bool driveCompensationUsed;    /**< Measured path scaled the last command. */
  float scale;                   /**< Rate-limited multiplier actually applied,
                                           between the measured path and the model. */
  bool scaleReady;               /**< The multiplier has taken a first value. */
} VP37Thermal;

/** @brief Shunt scan bookkeeping and the newest reduced block. */
typedef struct {
  VP37CurrentPulseResult cycleResult; /**< Newest reduced scan block, kept for
                                         the core-0 `VP37 IPULSE` report. */
  hal_status_t cycleResultStatus;
  uint32_t cycleResultSequence; /**< Increments once per reduced block. */
  uint32_t
      collectUs; /**< Time spent collecting and reducing the latest block. */
  uint32_t reducedUs;    /**< MCU time when the latest reduction completed. */
  uint32_t lastSequence; /**< Scan block sequence last retained. */
  uint32_t blocks;       /**< Blocks retained since start. */
  uint32_t gaps;         /**< Blocks the control loop never saw. */
  uint32_t frameNs;      /**< Frame period reported by the scan. */
  bool running;
} VP37Scan;

/** @brief One command before its application at the next PWM wrap. */
typedef struct {
  uint32_t writtenUs; /**< Local timestamp immediately before the PWM write. */
  float nominalPwm;   /**< FF+position PID, excluding current correction. */
  float voltageScale; /**< Local ADC calibration used by this command. */
  int32_t pwm; /**< Delivered duty including every correction and clamp. */
  bool driveSettled; /**< Quiet command, position and rail before this write. */
} VP37CurrentCommand;

/** @brief Bounded ON-current feedback and the command history it observes. */
typedef struct {
  VP37CurrentCommand history[VP37_CURRENT_CONTROL_HISTORY];
  VP37CurrentCommand
      matchedCommand; /**< Copy shared by this step's consumers. */
  uint32_t
      matchedSampleSequence; /**< Reduced-block sequence of the cached match. */
  bool
      matchCached; /**< No command was written since this match was resolved. */
  bool matchFound; /**< The cached observation matched one history command. */
  uint32_t count;
  uint32_t next;
  uint32_t lastCycleUs; /**< Last accepted rising edge; zero is valid. */
  uint32_t sampleAgeUs; /**< ON midpoint age when the control step used it. */
  float targetAmps; /**< Newest nominal position command in ON-current units. */
  float sampleTargetAmps; /**< Target belonging to the measured PWM period. */
  float measuredAmps;     /**< Guarded ON-phase mean [A]. */
  float errorAmps;        /**< Historical target minus measured current [A]. */
  float requestedPwm;     /**< Bounded proportional correction before slew. */
  float correctionPwm;    /**< Applied correction in nominal PWM counts. */
  bool enabled; /**< Bench C0/C1; normal control uses the same path. */
  bool active;  /**< A fresh observation matches a recorded command. */
  bool seen;    /**< At least one matching PWM period has been consumed. */
  uint32_t driveStableSinceUs; /**< Start of the present quiet window [us]. */
  uint32_t driveLastCommandUs; /**< Previous write, for continuity checks. */
  int32_t drivePositionReference; /**< Position anchor of the quiet window. */
  int32_t drivePwmReference; /**< Delivered-PWM anchor of the quiet window. */
  bool driveTracking; /**< The quiet-window anchors have been initialized. */
  bool driveSettled; /**< Latest command passed the resistance-learning gate. */
} VP37CurrentControl;

/** @brief The command as written to the actuator. */
typedef struct {
  float pwmValue;
  int32_t lastPWMval;
  int32_t finalPWM;
  bool pwmLimited;
} VP37Output;

/**
 * @brief One VP37 injection pump: lifecycle and timing, then one member per
 * unit of the module. The members are embedded, not pointed to, because
 * start.c copies the whole pump under vp37StateMutex and reads the copy on
 * core 0 without it; pointers would make that copy alias the live state.
 */
typedef struct {
  bool vp37Initialized;
  float pidTimeUpdate;
  uint32_t controlLastUs;
  uint32_t controlDtUs;
  uint32_t
      controlExecUs; /**< Execution time of the latest control step [us]. */
  uint32_t controlSequence;
  bool controlStarted;
  bool pidStarted;
  uint32_t pidLastUs, pidDtUs;
  VP37Feedback feedback;
  VP37Demand demand;
  VP37Feedforward feedforward;
  VP37Pid pid;
  VP37Supply supply;
  VP37Thermal thermal;
  VP37Scan scan;
  VP37CurrentControl currentControl;
  VP37Output output;
} VP37Pump;

/** @brief One control step; positions in Hz, elapsed time in us, output in PWM
 * counts. */
typedef struct {
  uint32_t us, dt,
      sequence;           /**< MCU timestamp, elapsed time and step number. */
  float requestedPercent; /**< Requested position [%]. */
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
  float thermalScale;          /**< Multiplier after the rate limit. */
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
 */
void VP37_showCurrentPulse(const VP37Pump *self);

/**
 * @brief Process one cycle of the VP37 inner quantity-actuator loop.
 * @note This is the low-level N146/G149-like loop. Higher-level requested-fuel-
 *       quantity arbitration is still represented only partially in the current
 * code.
 */
void VP37_process(VP37Pump *self);

/**
 * @brief Enable or disable the VP37 output stage.
 * @param self VP37 controller instance issuing the command.
 * @param enable True to enable the actuator path, false to disable it.
 * @note This is a project-local run/enable output and is only loosely
 * comparable to the OEM N109 stop-solenoid path.
 */
void VP37_enableVP37(VP37Pump *self, bool enable);

/** @brief Latch control off and remove PWM. Non-NULL self; restart requires
 * initialization. */
void VP37_stop(VP37Pump *self);

/**
 * @brief Read back the current VP37 enable output state.
 * @return True when VP37 output is enabled, otherwise false.
 * @note The signal is project-local and should not be treated as a literal N109
 * alias.
 */
bool VP37_isVP37Enabled(VP37Pump *self);

/**
 * @brief Print VP37 controller state for diagnostics.
 * @param self Snapshot copied while holding the controller owner mutex.
 * @note Call outside that mutex; this function performs serial and I2C I/O.
 */
void VP37_showDebug(VP37Pump *self);

/**
 * @brief Set the VP37 timing-actuator output as a normalized angle command.
 * @param self VP37 controller instance issuing the command.
 * @param angle Requested timing angle in the 0..100 range.
 * @note In OEM terminology this is closest to commanding the N108
 * start-of-injection actuator path. Closed-loop G80/G28 SOI feedback is not
 * implemented here yet.
 */
void VP37_setInjectionTiming(VP37Pump *self, int32_t angle);

/**
 * @brief Set the quantity actuator's position in percent of the stroke.
 * @param self Controller instance; NULL returns HAL_EINVAL.
 * @param percent Position across the usable stroke, clamped to 0..100%;
 * 100 % is VP37_PHYSICAL_LIMIT_PERCENT of the calibrated travel.
 * @return HAL_OK on acceptance, HAL_ESTATE before calibration, or HAL_EINVAL
 * for NULL/non-finite input. A non-finite value requests zero when calibrated.
 * @note This is the unit engine control works in: the percentage is mapped
 * onto the usable part of the calibrated stroke, so the same number means the
 * same position whatever the calibration produced. Every source then shares one
 * ramp, feedforward, PID and set of limits. Analog sensor conditioning belongs
 * to the sensor layer. Call on the control core under its state mutex. Zero
 * follows the shared descent and release policy.
 */
hal_status_t VP37_setPositionDemandPercentage(VP37Pump *self, float percent);

/**
 * @brief Set the quantity actuator's position in raw feedback counts.
 * @param self Controller instance; NULL returns HAL_EINVAL.
 * @param value Position in feedback counts, clamped to the calibrated range
 * between VP37_getPositionDemandMinValue() and
 * VP37_getPositionDemandMaxValue().
 * @return HAL_OK on acceptance, HAL_EINVAL for NULL, or HAL_ESTATE before
 * calibration.
 * @note The unit the position loop itself works in, so a caller addressing a
 * measured or recorded position hits it exactly instead of through a
 * percentage that rounds. Everything past the entry point is shared with
 * VP37_setPositionDemandPercentage(), including the percent the telemetry
 * reports.
 */
hal_status_t VP37_setPositionDemandValue(VP37Pump *self, int32_t value);

/**
 * @brief Lowest position VP37_setPositionDemandValue() accepts.
 * @param self Controller instance to inspect.
 * @return Calibrated bottom of the stroke in feedback counts, or -1 for NULL
 * and before calibration.
 */
int32_t VP37_getPositionDemandMinValue(const VP37Pump *self);

/**
 * @brief Highest position VP37_setPositionDemandValue() accepts.
 * @param self Controller instance to inspect.
 * @return The physical limit of the stroke (VP37_PHYSICAL_LIMIT_PERCENT of the
 * calibrated travel) in feedback counts, or -1 for NULL and before
 * calibration.
 */
int32_t VP37_getPositionDemandMaxValue(const VP37Pump *self);

/**
 * @brief Update VP37 PID gains and optionally reset controller state.
 * @param self VP37 controller instance to update.
 * @param kp New proportional gain.
 * @param ki New integral gain.
 * @param kd New base derivative gain [PWM*s/Hz], before the upper-target
 * addition.
 * @param shouldTriggerReset True to reset controller state after applying
 * gains.
 */
void VP37_setVP37PID(VP37Pump *self, float kp, float ki, float kd,
                     bool shouldTriggerReset);

/**
 * @brief Read back the configured VP37 PID gains before position scheduling.
 * @param self VP37 controller instance to inspect.
 * @param kp Output pointer receiving proportional gain, or NULL.
 * @param ki Output pointer receiving integral gain, or NULL.
 * @param kd Output pointer receiving base derivative gain [PWM*s/Hz], or NULL.
 */
void VP37_getVP37PIDValues(VP37Pump *self, float *kp, float *ki, float *kd);

/**
 * @brief Get the current VP37 PID update interval.
 * @return PID update time in milliseconds.
 */
float VP37_getVP37PIDTimeUpdate(VP37Pump *self);

#ifdef __cplusplus
}
#endif

#endif
