#ifndef T_FUEL
#define T_FUEL

#include <tools_c.h>

#include "config.h"
#include "sensors.h"
#include "tests.h"

#ifdef __cplusplus
extern "C" {
#endif

//fuel value read without average calculation
//#define JUST_RAW_FUEL_VAL

#define FUEL_MAX_SAMPLES 128
#define FUEL_INIT_VALUE -1

#define FUEL_MEASUREMENT_TIME_START 5
#define FUEL_MEASUREMENT_TIME_DEST 30


float readFuel(void);
void initFuelMeasurement(void);

#ifdef __cplusplus
}
#endif

#endif
