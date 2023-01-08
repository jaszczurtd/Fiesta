#include "glowPlugs.h"

//-----------------------------------------------------------------------------
// glow plugs
//-----------------------------------------------------------------------------

void glowPlugs(bool enable) {
  pcf8574_write(O_GLOW_PLUGS, enable);
}

void glowPlugsLamp(bool enable) {
  pcf8574_write(O_GLOW_PLUGS_LAMP, enable);
}

static int glowPlugsTime = 0;
static int glowPlugsLampTime = 0;
static int lastSecond = 0;

bool isGlowPlugsHeating(void) {
  return (glowPlugsTime > 0);
}

void initGlowPlugsTime(float temp) {

  if(temp < TEMP_MINIMUM_FOR_GLOW_PLUGS) {
    glowPlugsTime = int((-(temp) + 60.0) / 3.5);
    if(glowPlugsTime < 0) {
      glowPlugsTime = 0;
    }
  } else {
    glowPlugsTime = 0;
  }
  if(glowPlugsTime > 0) {
    glowPlugs(true);
    glowPlugsLamp(true);

    float divider = 3.0;
    if(temp >= 5.0) {
      divider = 8.0;
    }

    glowPlugsLampTime = int((float)glowPlugsTime / divider);

    lastSecond = getSeconds();
  }
}

void glowPlugsMainLoop(void) {
  if(glowPlugsTime >= 0) {
    if(getSeconds() != lastSecond) {
      lastSecond = getSeconds();

      if(glowPlugsTime-- <= 0) {
        glowPlugs(false);

        Serial.println("glow plugs disabled");
      }

      if(glowPlugsLampTime >= 0 && glowPlugsLampTime-- <= 0) {
        glowPlugsLamp(false);

        Serial.println("glow plugs lamp off");
      }
    }
  }  
}
