#ifndef T_GPS
#define T_GPS

#include <libConfig.h>
#include <tools_c.h>
#include <hal/hal_gps.h>
#include "../common/canDefinitions/canDefinitions.h"

#include "config.h"
#include "sensors.h"
#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the GPS module once.
 * @return None.
 */
void initGPS(void);

/**
 * @brief Clear persistent GPS date and time buffers.
 * @return None.
 */
void initGPSDateAndTime(void);

/**
 * @brief Poll GPS data and update global ECU values.
 * @return None.
 */
void getGPSData(void);

/**
 * @brief Get current vehicle speed derived from GPS.
 * @return Vehicle speed in km/h, or 0 when GPS is unavailable or below threshold.
 */
float getCurrentCarSpeed(void);

/**
 * @brief Get formatted GPS date text.
 * @return Pointer to the internal formatted date buffer.
 */
const char *getGPSDate(void);

/**
 * @brief Get formatted GPS time text.
 * @return Pointer to the internal formatted time buffer.
 */
const char *getGPSTime(void);

/**
 * @brief Check whether GPS data is currently valid and fresh enough to use.
 * @return True when GPS data is available, otherwise false.
 */
bool isGPSAvailable(void);

/**
 * @brief Get GPS date/time as a Unix epoch value.
 * @return Unix epoch timestamp, or 0 when GPS time is unavailable or invalid.
 */
uint32_t gpsGetEpoch(void);

#ifdef __cplusplus
}
#endif

#endif
