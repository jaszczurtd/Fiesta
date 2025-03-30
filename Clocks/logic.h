
#ifndef C_LOGIC
#define C_LOGIC

#include <Arduino.h>
#include <tools.h>
#include <canDefinitions.h>
#include <arduino-timer.h>
#include <multicoreWatchdog.h>

#include "TFTExtension.h"
#include "config.h"
#include "hardwareConfig.h"
#include "peripherials.h"
#include "hardwareConfig.h"
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

#endif
