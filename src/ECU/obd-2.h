#ifndef T_OBD
#define T_OBD

#include <hal/core/hal_status.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the OBD/UDS CAN responder.
 * @param retries Number of CAN initialization retries to attempt.
 * @return HAL_OK, or the error of the channel creation or of the request
 *         filters (logged, DTC_OBD_CAN_INIT_FAIL set, OBD stays off).
 */
hal_status_t obdInit(int retries);

/**
 * @brief Poll CAN and advance the OBD/ISO-TP state machine.
 */
void obdLoop(void);

#ifdef OBD_ENABLE_TOTDIST
/**
 * @brief Read the emulated total-distance value exposed through Ford DIDs.
 * @return Current odometer value in kilometers.
 */
uint32_t obdGetTotalDistanceKm(void);

/**
 * @brief Update the emulated total-distance value exposed through Ford DIDs.
 * @param km New odometer value in kilometers.
 */
void obdSetTotalDistanceKm(uint32_t km);
#endif

#ifdef __cplusplus
}
#endif

#endif
