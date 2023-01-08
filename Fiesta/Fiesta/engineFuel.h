#ifndef T_FUEL
#define T_FUEL

#include "utils.h"
#include "config.h"
#include "start.h"
#include "graphics.h"

#define FUEL_MAX_SAMPLES 128
#define FUEL_INIT_VALUE -1

#define FUEL_MEASUREMENT_TIME_START 3
#define FUEL_MEASUREMENT_TIME_DEST 30

float readFuel(void);
void initFuelMeasurement(void);

#endif