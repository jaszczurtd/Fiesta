
#ifndef ENGINE_MAPS
#define ENGINE_MAPS

#include <JaszczurHAL.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file engineMaps.h
 * @brief The tables the ECU shapes its outputs with, values only. Each
 * consumer owns the meaning of its columns and the interpolation; the numbers
 * live here so a calibration can change in one spot and a host test can check
 * the shape of every table.
 */

// ── N75 duty against engine speed and pedal position ─────────────────────────
#define RPM_PRESCALERS 8
#define N75_PERCENT_VALS 10

/** @brief N75 duty [%] per RPM band (rows, from 1500 in steps of 500) and
 * pedal position (columns). */
extern const int32_t RPM_table[RPM_PRESCALERS][N75_PERCENT_VALS];

// ── VP37 positive-demand feedforward ─────────────────────────────────────────
#define VP37_FF_KNOTS 9U
#define VP37_FF_COLUMNS 3U

/** @brief Holding map at 12 V and 49 C, one row per knot in ascending demand:
 * {demand [%], holding command [nominal PWM], upward-motion correction at the
 * reference rate [nominal PWM]}. The first knot sits at 0 and the last at
 * 100; the top motion value is the scale the runtime boost is a ratio to. */
extern const float VP37_FF_MAP[VP37_FF_KNOTS][VP37_FF_COLUMNS];

// ── VP37 stroke tapers: integral authority, gain and dead zone ───────────────
#define VP37_STROKE_TAPER_KNOTS 3U
#define VP37_STROKE_TAPER_COLUMNS 2U

/** @brief Integral authority, one row per knot in ascending demand:
 * {demand [%], limit [nominal PWM]}. Full below the taper start, easing to
 * the top value at full demand, where the stroke loses position authority. */
extern const float VP37_INTEGRAL_LIMIT_MAP[VP37_STROKE_TAPER_KNOTS]
                                          [VP37_STROKE_TAPER_COLUMNS];

/** @brief Proportional-gain multiplier along the stroke, same layout as the
 * other stroke tapers: {demand [%], multiplier}. The holding force drops in
 * two short steps near the top of the stroke; inside them a change of position
 * asks for less command, not more, and only the proportional gain holds the
 * actuator still. The base gain equals that slope, so the top needs more. */
extern const float VP37_PROPORTIONAL_GAIN_MAP[VP37_STROKE_TAPER_KNOTS]
                                             [VP37_STROKE_TAPER_COLUMNS];

/** @brief Share of the Adjustometer filter lag the proportional path takes
 * back, same layout: {demand [%], share 0..1}. Zero below the taper start:
 * there the loop is calm as it is, and the unfiltered sample would only bring
 * the actuator's PWM ripple into the command. */
extern const float VP37_FEEDBACK_LEAD_MAP[VP37_STROKE_TAPER_KNOTS]
                                         [VP37_STROKE_TAPER_COLUMNS];

/** @brief Largest position error the loop acts on, same layout: {demand [%],
 * limit [Hz]}. Far above any tracking lag below the taper start, then down to
 * the span the upper stroke can take without reaching its end stop. */
extern const float VP37_PROPORTIONAL_ERROR_LIMIT_MAP[VP37_STROKE_TAPER_KNOTS]
                                                    [VP37_STROKE_TAPER_COLUMNS];

/** @brief Integration dead zone, one row per knot in ascending demand:
 * {demand [%], zone [Hz]}. The base below the taper start, widening to the
 * top value at full demand. The table holds the default top; the bench may
 * move it at runtime. */
extern const float VP37_INTEGRAL_DEADBAND_MAP[VP37_STROKE_TAPER_KNOTS]
                                             [VP37_STROKE_TAPER_COLUMNS];

#ifdef __cplusplus
}
#endif

#endif
