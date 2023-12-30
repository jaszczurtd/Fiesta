#ifndef T_GLOWPLUGS
#define T_GLOWPLUGS

#include <tools.h>
#include <SmartTimers.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"

void glowPlugs(bool enable);
void glowPlugsLamp(bool enable);
bool isGlowPlugsHeating(void);
void initGlowPlugsTime(float temp);
void glowPlugsMainLoop(void);

#endif