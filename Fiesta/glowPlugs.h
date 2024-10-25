#ifndef T_GLOWPLUGS
#define T_GLOWPLUGS

#include <tools.h>
#include <SmartTimers.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"

#include "EngineController.h"

#define MAX_GLOW_PLUGS_TIME SECONDS_IN_MINUTE

class glowPlugs : public EngineController {
public:
  glowPlugs();
  void init() override;  
  void process() override;
  void showDebug() override;
  void enableGlowPlugs(bool enable);
  void glowPlugsLamp(bool enable);
  bool isGlowPlugsHeating(void);
  void initGlowPlugsTime(float temp);
  void glowPlugsMainLoop(void);

private:
  int glowPlugsTime;
  int glowPlugsLampTime;
  unsigned long lastSecond;
  bool warmAfterStart;

  void calculateGlowPlugsTime(float temp);
};

glowPlugs *getGlowPlugsInstance(void);
void createGlowPlugs(void);


#endif