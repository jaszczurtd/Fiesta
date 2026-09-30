#ifndef VP37_TUNING_H
#define VP37_TUNING_H

/**
 * @file vp37_tuning.h
 * @brief Run-time tuning of one pump: the gains, limits and switches a bench
 * console may change. The module checks every value against its range; call
 * on the control core.
 */

#include "vp37.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Tuning parameters; every value is finite and non-negative. */
typedef enum {
  VP37_TUNING_KP = 0,              /**< Proportional gain [PWM/Hz]. */
  VP37_TUNING_KI,                  /**< Integral gain [PWM/(Hz*s)]. */
  VP37_TUNING_KD,                  /**< Base derivative gain [PWM*s/Hz]. */
  VP37_TUNING_TOP_KD,              /**< Upper-target addition, to
                                        VP37_PID_TOP_KD_MAX. */
  VP37_TUNING_PERIOD_MS,           /**< Control period, VP37_PID_TIME_UPDATE_MIN
                                        to VP37_PID_TIME_UPDATE_MAX [ms]. */
  VP37_TUNING_DERIVATIVE_FILTER,   /**< Derivative filter time constant [s]. */
  VP37_TUNING_INTEGRAL_CAP,        /**< Integral cap [nominal PWM], to
                                        VP37_BENCH_INTEGRAL_LIMIT_MAX; zero
                                        follows the position profile. */
  VP37_TUNING_TEMPERATURE_WEIGHT,  /**< Weight of the thermal multiplier, to 1.
                                    */
  VP37_TUNING_HOLD_CONFIRM_MS,     /**< Whole ms inside the band before the
                                        integral holds, to
                                        VP37_INTEGRAL_HOLD_CONFIRM_MAX_MS. */
  VP37_TUNING_DEADBAND_TOP_HZ,     /**< Integral dead zone at the top, to
                                        VP37_PID_DEADBAND_TOP_MAX_HZ. */
  VP37_TUNING_MOTION_BOOST_UP,     /**< Rising motion assist [nominal PWM], to
                                        VP37_PWM_FF_MOTION_BOOST_MAX. */
  VP37_TUNING_MOTION_BOOST_DOWN,   /**< Falling motion assist, same range. */
  VP37_TUNING_TOP_ARRIVAL_DECEL,   /**< Rising-ramp brake into the top,
                                        [%/s^2], zero off. */
  VP37_TUNING_MOTION_RATE_CAP,     /**< Ramp rate the assist follows up to
                                        [%/s], zero follows the full slew. */
  VP37_TUNING_CURRENT_OBSERVATION, /**< 0/1: learn the drive current. */
  VP37_TUNING_CURRENT_FEEDBACK,    /**< 0/1: ON-current feedback. */
  VP37_TUNING_MAP_TRIM,            /**< 0/1; either value clears the learned
                                        trim. */
  VP37_TUNING_DRIVE_COMPENSATION,  /**< 0/1; off forgets the learned drive
                                        resistance. */
  VP37_TUNING_CYCLE_VOLTAGE,       /**< 0/1: full-period supply mean. */
  VP37_TUNING_SUPPLY_FROZEN,       /**< 0/1: hold the supply multiplier. */
  VP37_TUNING_COUNT
} VP37TuningParam;

/**
 * @brief Set one tuning parameter.
 * @return HAL_OK, or HAL_EINVAL for NULL, an unknown parameter or a value
 * outside its range, which leaves the pump unchanged.
 */
hal_status_t VP37_setTuning(VP37Pump *self, VP37TuningParam param, float value);

/**
 * @brief Read one tuning parameter back; switches read 0 or 1.
 * @return The value, or 0 for an unknown parameter.
 */
float VP37_getTuning(const VP37Pump *self, VP37TuningParam param);

/**
 * @brief Return the gains, limits and assists to their bench defaults and
 * reset the PID. The switches keep their state.
 */
void VP37_resetTuning(VP37Pump *self);

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
void VP37_getVP37PIDValues(const VP37Pump *self, float *kp, float *ki,
                           float *kd);

/**
 * @brief Get the current VP37 PID update interval.
 * @return PID update time in milliseconds.
 */
float VP37_getVP37PIDTimeUpdate(const VP37Pump *self);

#ifdef __cplusplus
}
#endif

#endif
