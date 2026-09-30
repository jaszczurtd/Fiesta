#ifndef VP37_ADAPTER_H
#define VP37_ADAPTER_H

/**
 * @file vp37_adapter.h
 * @brief The ECU side of the VP37 module: the board services the pump runs
 * through, its start and what the ECU takes from its published status.
 */

#include "../common/vp37/vp37.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The ECU's board services: the PWM channels without the DTC store,
 * the PCF8574 enable output, the engine speed guard, the watchdog and the
 * bench test context.
 */
const VP37Callbacks *vp37AdapterCallbacks(void);

/**
 * @brief Check the drive outputs, install the board services and start the
 * pump (baseline wait and stroke calibration).
 * @param pump Pump to start.
 * @return VP37_INIT_OUTPUT_UNAVAILABLE without both PWM channels, which also
 * raises DTC_PWM_CHANNEL_NOT_INIT; otherwise the VP37_init() result.
 * @note Core 0, at start-up. Publishes the fuel temperature and supply the
 * calibration read.
 */
VP37InitStatus vp37AdapterStart(VP37Pump *pump);

/**
 * @brief Copy the fuel temperature and supply of a new Adjustometer frame from
 * the published status into F_FUEL_TEMP and F_VOLTS.
 * @param pump Pump; only its published status is read.
 * @note Core 0, every loop pass: the control core never waits on the global
 * values.
 */
void vp37AdapterPublish(const VP37Pump *pump);

#ifdef __cplusplus
}
#endif

#endif
