#ifndef S_OIL_PRESSURE_H
#define S_OIL_PRESSURE_H

#include "can.h"
#include "speed.h"
#include "config.h"
#include "hardwareConfig.h"

// Oil pressure sender parameters
#define OIL_PRESSURE_FILTER_ALPHA 0.20f
#define OIL_PRESSURE_MAX_BAR 10.0f
#define OIL_PRESSURE_SENSOR_RES_MIN_OHM 10.0f
#define OIL_PRESSURE_SENSOR_RES_MAX_OHM 180.0f
#define OIL_PRESSURE_PULLUP_OHM 220.0f
#define OIL_PRESSURE_ADC_REF_V 3.3f

#define OIL_PRESSURE_ADC_BITS 12
// true  -> pull-up resistor to Vref, sensor to GND
// false -> pull-down resistor to GND, sensor to Vref
#define OIL_PRESSURE_DIVIDER_PULLUP 1

bool setupOilPressure(void);
bool readOilPressure(void *arg);

#endif
