#ifndef VP37_CONFIG_H
#define VP37_CONFIG_H

/**
 * @file vp37_config.h
 * @brief Compile-time constants of the VP37 module: build switches, control
 * gains and limits, compensation, slew and calibration parameters.
 */

#include <JaszczurHAL.h>

#include "../common/vp37_drive_config.h"
#include "vp37_maps.h"
#include "vp37_power_stage.h"

/** Bench telemetry and the RAM trace; the ECU derives it from its
 * functional-test image in hal_project_config.h. */
#ifndef VP37_TELEMETRY_ENABLED
#define VP37_TELEMETRY_ENABLED 0
#endif

/** Supply the holding map was measured at [V]; the command is scaled by this
 * over the measured supply. */
#define VP37_NOMINAL_VOLTAGE 12.0f

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

/* Telemetry exists only in the bench image, dense enough to follow single
 * control steps. */
#if VP37_TELEMETRY_ENABLED
/** Poll period of VP37_waitForPublication() [us]. */
#define VP37_PUBLICATION_POLL_US 20U
#define VP37_DEBUG_UPDATE 20U
#define VP37_TELEMETRY_UPDATE 500U
/** @brief Steps in a bench RAM capture. Each step costs one VP37TraceSample
 * of static RAM, so this is the knob to turn when a build runs out. */
#ifndef VP37_TRACE_SAMPLES
#define VP37_TRACE_SAMPLES 1024U
#endif
#endif
/** Maximum age of the supply averaging window's midpoint [us]. */
#define VP37_CYCLE_VOLTAGE_MAX_AGE_US 20000U
/** Time constant of the supply slope used to predict PWM application [s]. */
#define VP37_VOLTAGE_SLOPE_FILTER_S 0.02f
/** Maximum voltage lead added to a fresh supply mean [V]. */
#define VP37_VOLTAGE_PREDICTION_LIMIT_V 0.5f

#define DEFAULT_INJECTION_PRESSURE 300 // bar

#define VP37_PID_TIME_UPDATE 5.0f // minimum control period [ms]
// Control periods the step accepts; outside them the pump stops [ms].
#define VP37_PID_TIME_UPDATE_MIN 1.0f
#define VP37_PID_TIME_UPDATE_MAX 100.0f
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
/** Bench ceiling for the upper-target derivative gain. */
#define VP37_PID_TOP_KD_MAX 0.01f
/** Time constant for engaging and releasing upper-target damping [s]. */
#define VP37_PID_TOP_D_BLEND_S 0.05f
// Derivative filter time constant [s].
#define VP37_PID_TF 0.003f
// Column order of the stroke tapers in vp37_maps.h: {demand [%], value}.
#define VP37_TAPER_COL_PERCENT 0U
#define VP37_TAPER_COL_VALUE 1U
// Preserve integral output authority when changing Ki (nominal PWM counts).
// The residual authority above the holding map and its taper near the upper
// endpoint are the VP37_INTEGRAL_LIMIT_MAP rows in vp37_maps.h.
#define VP37_PID_TRIM_PWM (VP37_INTEGRAL_LIMIT_MAP[0U][VP37_TAPER_COL_VALUE])
#define VP37_BENCH_INTEGRAL_CAP_PWM VP37_PID_TRIM_PWM
/** Bench ceiling for the integral cap [nominal PWM]. */
#define VP37_BENCH_INTEGRAL_LIMIT_MAX 360.0f
#define VP37_PID_MAX_INTEGRAL (VP37_PID_TRIM_PWM / VP37_PID_KI)

// Continuous dead zone for integration only; P and D remain active. Above the
// taper start the stroke loses position authority: the same command settles
// hundreds of hertz apart and the same current holds very different
// positions. Integration there walks until the mechanism breaks free and
// produces a roughly 1 Hz relaxation cycle, so the dead zone widens with
// position to a band that covers the insensitive range; the rows are
// VP37_INTEGRAL_DEADBAND_MAP in vp37_maps.h. A zero top keeps the base dead
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
#define VP37_INTEGRAL_HOLD_CONFIRM_MAX_MS 1000U
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
// Column order of the holding map in vp37_maps.h: {demand [%], holding
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
// The upward assist follows the ramp rate up to this rate [% of travel/s] and
// stays there above it; bench knob X5, zero follows the full slew. A tracked
// pot turn faster than the slew runs the ramp at 300/275 %/s, and the assist
// for that rate makes the actuator lead the ramp by 260-500 Hz into the top,
// where it rang hot (FT 43-50 C, bench 2026-09-27); a 250 %/s ramp led by
// 150 Hz and settled. Capping the ramp itself only widened the lead.
#define VP37_PWM_FF_MOTION_RATE_CAP_PERCENT_PER_S 250.0f
#define VP37_PWM_FF_MOTION_RATE_CAP_MAX_PERCENT_PER_S 2000.0f
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
#define VP37_PWM_MAX VP37_PWM_RESOLUTION

// Calibration samples are spaced in time and accepted only after a complete
// window is stable.  This lets a warm actuator take longer than the old fixed
// 200 ms delay without slowing a normally settling actuator unnecessarily.
#define VP37_CALIBRATION_SAMPLE_INTERVAL_MS 20
#define VP37_CALIBRATION_MIN_SETTLE_MS 200
#define VP37_CALIBRATION_TIMEOUT_MS 1000
#define VP37_CALIBRATION_STABLE_SAMPLES 6
#define VP37_CALIBRATION_STABLE_SPAN_HZ 40
#define VP37_CALIBRATION_MIN_TRAVEL_HZ 6000

// Maximum wait for Adjustometer baseline calibration at startup [ms].
// Must accommodate the oscillator warm-up period (ADJUSTOMETER_WARMUP_MS
// on the Adjustometer side) plus convergence (250 ms) plus post-convergence
// verification (1000 ms).  Extra margin handles repeated convergence restarts
// caused by slow oscillator drift on cold power-on.
#define VP37_ADJUSTOMETER_BASELINE_WAIT_MS 8000

// define this, to avoid magic numbers in the code
#define VP37_PERCENT_MIN 0
#define VP37_PERCENT_MAX 100
/** Physical top of the usable stroke [% of the calibrated travel]. Demand
 * 0..100 % maps onto 0..this share of the travel, so the upper stroke with its
 * negative-stiffness steps (from ~90 %, static sweep 2026-09-20) stays out of
 * reach; the maximum fuel quantity is cut accordingly. In cold fuel the band
 * just under 86 % is soft as well: a 30 s full-demand hold there hopped up to
 * the target in two of three tries, at 85 % in none (bench 2026-09-28, FT
 * 34 C). 100 restores the full travel. The stroke maps (holding map, tapers,
 * upper damping) keep the physical scale. */
#define VP37_PHYSICAL_LIMIT_PERCENT 85.0f
/** Optional deceleration of every rising ramp into the top of the demand
 * range [% of travel/s^2]; bench knob X2. Zero disables it and a standing
 * target keeps its own arrival brake. Off since the Adjustometer averages one
 * drive period instead of an EMA: the tracked arrival then overshoots about
 * 200 Hz below the negative-stiffness steps and settles without ringing, while
 * 750 braked from 30 % of the range and 3000 still delayed the arrival by
 * 100 ms and deepened the sag after it (bench 2026-09-27). */
#define VP37_TOP_ARRIVAL_DECEL_PERCENT_PER_S2 0.0f
#define VP37_TOP_ARRIVAL_DECEL_MAX_PERCENT_PER_S2 20000.0f
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
#define TIMING_PWM_MAX VP37_PWM_RESOLUTION

#endif
