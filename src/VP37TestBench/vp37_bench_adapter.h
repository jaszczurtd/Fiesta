#ifndef VP37_BENCH_ADAPTER_H
#define VP37_BENCH_ADAPTER_H

/**
 * @file vp37_bench_adapter.h
 * @brief The bench side of the VP37 module: the board services the pump runs
 * through on the test stand, its start, and the demand potentiometer.
 */

#include "../common/vp37/vp37.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The bench's board services: the twin power-stage PWM channels, the
 * drive enable on a plain GPIO, the Adjustometer bus transfer and the
 * watchdog. The drive is always allowed; there is no engine to guard.
 */
const VP37Callbacks *vp37BenchCallbacks(void);

/**
 * @brief Create the drive outputs, install the board services and start the
 * pump (baseline wait and stroke calibration).
 * @param pump Pump to start.
 * @return VP37_INIT_ALREADY_INITIALIZED for a pump that is already running,
 * before anything touches the hardware, so the running pump keeps its drive;
 * VP37_INIT_OUTPUT_UNAVAILABLE when a PWM channel cannot be created;
 * otherwise the VP37_init() result. Without a pump attached the start ends
 * as VP37_INIT_BASELINE_NOT_READY after the baseline timeout and the bench
 * keeps running with the pump stopped.
 * @note Call on the core that will run VP37_process(): the scan's completion
 * interrupt belongs to the core that created it.
 */
VP37InitStatus vp37BenchAdapterStart(VP37Pump *pump);

/**
 * @brief The demand of the bench potentiometer.
 * @return Position across the usable stroke [0..100 %].
 * @note The pot divider feeds the auxiliary scan input, so the reading works
 * the same before the scan starts and while it runs.
 */
float vp37BenchDemandPercent(void);

#ifdef UNIT_TEST
/** @brief The drive output channels, for the adapter's host tests. */
hal_pwm_freq_channel_t vp37BenchQuantityChannel(void);
hal_pwm_freq_channel_t vp37BenchTimingChannel(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
