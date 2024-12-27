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
  initialized = false;
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

void glowPlugs::calculateGlowPlugLampTime(float temp) {
  if (temp <= TEMP_VERY_LOW) {
    // Maximum time at very low temperature
    glowPlugsLampTime = MAX_LAMP_TIME;
  } else if (temp < TEMP_MINIMUM_FOR_GLOW_PLUGS) {
    // Proportional reduction in time within the range -20 to 10 degrees
    float tempDiff = TEMP_MINIMUM_FOR_GLOW_PLUGS - temp;
    float scaleFactor = tempDiff / (TEMP_MINIMUM_FOR_GLOW_PLUGS * 2);  // Scale factor based on temperature difference
    glowPlugsLampTime = MAX_LAMP_TIME * scaleFactor;
  } else {
    // Minimum lamp time for higher temperatures
    glowPlugsLampTime = MIN_LAMP_TIME;
  }

  // Ensure the lamp time is non-negative
  glowPlugsLampTime = (glowPlugsLampTime < 0) ? 0 : glowPlugsLampTime;
}

void glowPlugs::initGlowPlugsTime(float temp) {

  calculateGlowPlugsTime(temp);

  if(glowPlugsTime > 0) {
    enableGlowPlugs(true);
    glowPlugsLamp(true);

    calculateGlowPlugLampTime(temp);

    lastSecond = getSeconds();
  }

  initialized = true;
}

void glowPlugs::process(void) {

  if(!initialized) {
    return;
  }

  float temp = valueFields[F_COOLANT_TEMP];
  if(temp > TEMP_COLD_ENGINE) {
    warmAfterStart = true;
  }

  if(!warmAfterStart) {
    if(temp <= TEMP_COLD_ENGINE &&
      valueFields[F_RPM] > RPM_MIN) {
        calculateGlowPlugsTime(temp);
        if(glowPlugsTime > 0) {
          enableGlowPlugs(true);
          warmAfterStart = true;
        }
    }
  }

  if(glowPlugsTime > 0 || glowPlugsLampTime > 0) {
    if(glowPlugsTime != lastglowPlugsTime || glowPlugsLampTime != lastglowPlugsLampTime) {
      lastglowPlugsTime = glowPlugsTime;
      lastglowPlugsLampTime = glowPlugsLampTime;
      deb("glowPlugsTime: %d %d", glowPlugsTime, glowPlugsLampTime);
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

