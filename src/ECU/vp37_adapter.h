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
 * the published status into F_FUEL_TEMP and F_VOLTS, and report a lasting fuel
 * sensor fault.
 * @param pump Pump; only its published status is read.
 * @note Core 0, every loop pass: the control core never waits on the global
 * values. Without a valid fuel temperature (sensor fault, lost link, no frame
 * yet) F_FUEL_TEMP is 0 and isFuelTempValid() false. A fault reported by every
 * good frame for ECU_FUEL_TEMP_FAULT_CONFIRM_MS raises
 * DTC_ADJ_FUEL_TEMP_BROKEN; the first good frame without it clears the active
 * flag.
 */
void vp37AdapterPublish(const VP37Pump *pump);

/**
 * @brief Whether F_FUEL_TEMP holds a measurement.
 * @return False after a fuel sensor fault, a lost Adjustometer link or before
 * the first good frame; CAN and OBD then send no fuel temperature.
 * @note Core 0, like vp37AdapterPublish().
 */
bool isFuelTempValid(void);

#ifdef UNIT_TEST
/** @brief Forget what earlier tests published. */
void vp37AdapterResetForTest(void);
#endif

/**
 * @brief Allow core-1 services outside the upcoming VP37 deadline guard.
 * @param pump Controller owned by core 1; NULL is rejected.
 * @param nowUs Current MCU time in microseconds, modulo 2^32.
 * @return True outside the guard or after a started controller is already due.
 * @note An overloaded controller must still receive demand and stop commands.
 */
bool vp37AdapterCanServiceCore1(const VP37Pump *pump, uint32_t nowUs);

#ifdef __cplusplus
}
#endif

#endif
