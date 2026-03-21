
#ifndef C_LOGIC
#define C_LOGIC

#include <tools.h>
#include <canDefinitions.h>
#include <SmartTimers.h>
#include <multicoreWatchdog.h>

#include <hal/hal.h>
#include "TFTExtension.h"
#include "config.h"
#include "hardwareConfig.h"
#include "peripherials.h"
#include "can.h"
#include "buzzer.h"
#include "Cluster.h"

void setup_a(void);
void loop_a(void);
void setup_b(void);
void loop_b(void);

bool alertSwitch(void);
bool seriousAlertSwitch(void);
void triggerDrawHighImportanceValue(bool state);
void updateCluster(void);

#endif
