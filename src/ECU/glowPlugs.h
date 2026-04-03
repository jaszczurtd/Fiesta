#ifndef T_GLOWPLUGS
#define T_GLOWPLUGS

#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"

typedef struct {
  int glowPlugsTime;
  int glowPlugsLampTime;
  int lastGlowPlugsTime;
  int lastGlowPlugsLampTime;

  unsigned long lastSecond;
  bool warmAfterStart;
  bool initialized;
} glowPlugs;

void glowPlugs_init(glowPlugs *self);
void glowPlugs_process(glowPlugs *self);
void glowPlugs_showDebug(glowPlugs *self);
void glowPlugs_enableGlowPlugs(glowPlugs *self, bool enable);
void glowPlugs_glowPlugsLamp(glowPlugs *self, bool enable);
bool glowPlugs_isGlowPlugsHeating(glowPlugs *self);
void glowPlugs_initGlowPlugsTime(glowPlugs *self, float temp);

glowPlugs *getGlowPlugsInstance(void);
void createGlowPlugs(void);

#endif
