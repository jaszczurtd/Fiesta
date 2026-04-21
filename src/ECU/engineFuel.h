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


/**
 * @brief Read the fuel level, including averaging logic when enabled.
 * @return Current filtered or raw fuel level value, depending on build flags.
 */
float readFuel(void);

/**
 * @brief Reset the fuel measurement buffer and timing state.
 * @return None.
 */
void initFuelMeasurement(void);

#ifdef __cplusplus
}
#endif

#endif
