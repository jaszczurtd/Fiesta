#ifndef T_GLOWPLUGS
#define T_GLOWPLUGS

#include "utils.h"
#include "config.h"
#include "start.h"

void glowPlugs(bool enable);
void glowPlugsLamp(bool enable);
bool isGlowPlugsHeating(void);
void initGlowPlugsTime(float temp);
void glowPlugsMainLoop(void);

#endif