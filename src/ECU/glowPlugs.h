#ifndef T_GLOWPLUGS
#define T_GLOWPLUGS

#include <tools.h>
#include <SmartTimers.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"

class glowPlugs {
public:
  glowPlugs();
  void init();  
  void process();
  void showDebug();
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