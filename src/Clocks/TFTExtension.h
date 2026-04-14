#ifndef T_TFT_EXTENSION
#define T_TFT_EXTENSION

#include <tools.h>
#include <hal/hal_display.h>
#include "hardwareConfig.h"
#include "tempGauge.h"
#include "simpleGauge.h"
#include "pressureGauge.h"
#include "logic.h"
#include "engineFuel.h"
#include "can.h"
#include "icons.h"

void initTFT(void);
void softInitDisplay(void);
void redrawAllGauges(void);

#endif
