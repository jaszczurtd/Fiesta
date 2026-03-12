#ifndef S_OIL_PRESSURE_H
#define S_OIL_PRESSURE_H

#include <Arduino.h>

#include "config.h"
#include "hardwareConfig.h"

bool setupOilPressure(void);
bool readOilPressure(void *arg);

#endif
