#ifndef S_START_H
#define S_START_H

#include <Arduino.h>
#include <tools.h>
#include <canDefinitions.h>
#include <arduino-timer.h>
#include <multicoreWatchdog.h>

#include "can.h"
#include "oilPressure.h"
#include "speed.h"
#include "peripherials.h"
#include "hardwareConfig.h"
#include "config.h"

void initialization(void);
void looper();
void initialization1();
void looper1();


#endif
