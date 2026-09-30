#ifndef VP37_MAPS
#define VP37_MAPS

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file vp37_maps.h
 * @brief The VP37 tables, values only: the holding map and the integral
 * authority and dead zone along the stroke. The control units own the meaning
 * of the columns and the interpolation (column indices in vp37_config.h).
 */

// ── VP37 positive-demand feedforward ─────────────────────────────────────────
#define VP37_FF_KNOTS 9U
#define VP37_FF_COLUMNS 3U

/** @brief Holding map at 12 V and 49 C, one row per knot in ascending demand:
 * {demand [%], holding command [nominal PWM], upward-motion correction at the
 * reference rate [nominal PWM]}. The first knot sits at 0 and the last at
 * 100; the top motion value is the scale the runtime boost is a ratio to. */
extern const float VP37_FF_MAP[VP37_FF_KNOTS][VP37_FF_COLUMNS];

// ── VP37 integral authority and dead zone along the stroke ───────────────────
#define VP37_STROKE_TAPER_KNOTS 4U
#define VP37_STROKE_TAPER_COLUMNS 2U

/** @brief Integral authority, one row per knot in ascending demand:
 * {demand [%], limit [nominal PWM]}. Full below the taper start, easing to
 * the top value at full demand, where the stroke loses position authority. */
extern const float VP37_INTEGRAL_LIMIT_MAP[VP37_STROKE_TAPER_KNOTS]
                                          [VP37_STROKE_TAPER_COLUMNS];

/** @brief Integration dead zone, one row per knot in ascending demand:
 * {demand [%], zone [Hz]}. The base below the taper start, widening gently
 * to the bend and steeply from there to the top value at full demand. The
 * table holds the default top; the bench may move it at runtime. */
extern const float VP37_INTEGRAL_DEADBAND_MAP[VP37_STROKE_TAPER_KNOTS]
                                             [VP37_STROKE_TAPER_COLUMNS];

#ifdef __cplusplus
}
#endif

#endif
