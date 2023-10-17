#ifndef T_OIL
#define T_OIL
#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"
#include "TFTExtension.h"

void redrawOilPressure(void);
void showOilPressureAmount(float current);
float readOilBarPressure(void);

#endif
