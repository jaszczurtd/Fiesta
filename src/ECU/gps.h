#ifndef T_GPS
#define T_GPS

#include <libConfig.h>
#include <tools.h>
#include <hal/hal_gps.h>
#include "canDefinitions.h"

#include "config.h"
#include "sensors.h"
#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

void initGPS(void);
void initGPSDateAndTime(void);
void getGPSData(void);
float getCurrentCarSpeed(void);
const char *getGPSDate(void);
const char *getGPSTime(void);
bool isGPSAvailable(void);
uint32_t gpsGetEpoch(void);

#ifdef __cplusplus
}
#endif

#endif
