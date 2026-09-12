#ifndef T_OBD
#define T_OBD

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the OBD/UDS CAN responder.
 * @param retries Number of CAN initialization retries to attempt.
 * @return None.
 */
void obdInit(int retries);

/**
 * @brief Poll CAN and advance the OBD/ISO-TP state machine.
 * @return None.
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
 * @return None.
 */
void obdSetTotalDistanceKm(uint32_t km);
#endif

#ifdef __cplusplus
}
#endif

#endif
