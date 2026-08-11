#ifndef T_TFT_EXTENSION
#define T_TFT_EXTENSION

#include "can.h"
#include "engineFuel.h"
#include "hardwareConfig.h"
#include "icons.h"
#include "logic.h"
#include "pressureGauge.h"
#include "simpleGauge.h"
#include "tempGauge.h"
#include <hal/display/hal_display.h>
#include <tools.h>

void initTFT(void);
void softInitDisplay(void);
void redrawAllGauges(void);

#endif
