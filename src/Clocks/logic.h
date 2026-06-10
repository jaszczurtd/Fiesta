
#ifndef C_LOGIC
#define C_LOGIC

#include "../common/canDefinitions/canDefinitions.h"
#include <tools.h>

#include "Cluster.h"
#include "TFTExtension.h"
#include "buzzer.h"
#include "can.h"
#include "config.h"
#include "hardwareConfig.h"
#include "peripherials.h"
#include <hal/hal.h>
#include <hal/hal_display.h>

void initialization(void);
void looper(void);
void initialization1(void);
void looper1(void);

bool alertSwitch(void);
bool seriousAlertSwitch(void);
void triggerDrawHighImportanceValue(bool state);
void updateCluster(void);

#endif
