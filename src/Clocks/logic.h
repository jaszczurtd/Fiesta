
#ifndef C_LOGIC
#define C_LOGIC

#include "../common/canDefinitions/canDefinitions.h"
#include <JaszczurHAL.h>

#include "Cluster.h"
#include "TFTExtension.h"
#include "buzzer.h"
#include "can.h"
#include "config.h"
#include "hardwareConfig.h"
#include "peripherials.h"
#include <hal/display/hal_display.h>
#include <hal/hal.h>

bool alertSwitch(void);
bool seriousAlertSwitch(void);
void triggerDrawHighImportanceValue(bool state);
void updateCluster(void);

#endif
