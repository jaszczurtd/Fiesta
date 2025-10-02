#ifndef T_GLOWPLUGS
#define T_GLOWPLUGS

#include <tools.h>
#include <SmartTimers.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"

#include "EngineController.h"

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
  int lastGlowPlugsTime;
  int lastGlowPlugsLampTime;  

  unsigned long lastSecond;
  bool warmAfterStart;
  bool initialized;

  void calculateGlowPlugsTime(float temp);
  void calculateGlowPlugLampTime(float temp);
};

glowPlugs *getGlowPlugsInstance(void);
void createGlowPlugs(void);


#endif