#include "glowPlugs.h"

//-----------------------------------------------------------------------------
// glow plugs
//-----------------------------------------------------------------------------

static glowPlugs *glowP = nullptr;
void createGlowPlugs(void) {
  glowP = new glowPlugs();
  glowP->init();
}

glowPlugs *getGlowPlugsInstance(void) {
  if(glowP == nullptr) {
    createGlowPlugs();
  }
  return glowP;
}

glowPlugs::glowPlugs() { }

void glowPlugs::init() {
  glowPlugsTime = 0;
  glowPlugsLampTime = 0;
  lastSecond = 0;
  warmAfterStart = false;
}

void glowPlugs::enableGlowPlugs(bool enable) {
  pcf8574_write(PCF8574_O_GLOW_PLUGS, enable);
}

void glowPlugs::glowPlugsLamp(bool enable) {
  pcf8574_write(PCF8574_O_GLOW_PLUGS_LAMP, enable);
}

bool glowPlugs::isGlowPlugsHeating(void) {
  return (glowPlugsTime > 0);
}

void glowPlugs::calculateGlowPlugsTime(float temp) {
  if(temp < TEMP_MINIMUM_FOR_GLOW_PLUGS) {
    glowPlugsTime = (int)(MAX_GLOW_PLUGS_TIME * (TEMP_MINIMUM_FOR_GLOW_PLUGS - temp) / TEMP_MINIMUM_FOR_GLOW_PLUGS);
  } else {
    glowPlugsTime = 0;
  }
}

void glowPlugs::initGlowPlugsTime(float temp) {

  calculateGlowPlugsTime(temp);

  if(glowPlugsTime > 0) {
    enableGlowPlugs(true);
    glowPlugsLamp(true);

    //TODO: get rid of these magic values oneday
    float divider = 3.0;
    if(temp >= 5.0) {
      divider = 8.0;
    }

    glowPlugsLampTime = int((float)glowPlugsTime / divider);

    lastSecond = getSeconds();
  }
}

void glowPlugs::process(void) {

  float temp = valueFields[F_COOLANT_TEMP];
  if(temp > TEMP_COLD_ENGINE) {
    warmAfterStart = true;
  }

  if(!warmAfterStart) {
    if(temp <= TEMP_COLD_ENGINE &&
      valueFields[F_RPM] > RPM_MIN) {
        calculateGlowPlugsTime(temp);
        if(glowPlugsTime > 0) {
          glowPlugsTime *= TEMP_HEATING_GLOW_PLUGS_MULTIPLIER;
          enableGlowPlugs(true);
          warmAfterStart = true;
        }
    }
  }

  if(glowPlugsTime >= 0) {
    if(getSeconds() != lastSecond) {
      lastSecond = getSeconds();

      if(glowPlugsTime-- <= 0) {
        enableGlowPlugs(false);

        deb("glow plugs disabled");
      }

      if(glowPlugsLampTime >= 0 && glowPlugsLampTime-- <= 0) {
        glowPlugsLamp(false);

        deb("glow plugs lamp off");
      }
    }
  }  
}

void glowPlugs::showDebug() {
  deb("glowPlugs: %d", isGlowPlugsHeating());
}

