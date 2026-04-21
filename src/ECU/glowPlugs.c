#include "glowPlugs.h"
#include "ecuContext.h"

//-----------------------------------------------------------------------------
// glow plugs
//-----------------------------------------------------------------------------

void createGlowPlugs(void) {
  glowPlugs_init(getGlowPlugsInstance());
}

glowPlugs *getGlowPlugsInstance(void) {
  return &getECUContext()->glowP;
}

/**
 * @brief Calculate glow plug heating time from coolant temperature.
 * @param self Glow plug controller instance to update.
 * @param temp Coolant temperature used for the calculation.
 * @return None.
 */
static void glowPlugs_calculateGlowPlugsTime(glowPlugs *self, float temp) {
  if(temp < TEMP_MINIMUM_FOR_GLOW_PLUGS) {
    self->glowPlugsTime = (int32_t)(MAX_GLOW_PLUGS_TIME * (TEMP_MINIMUM_FOR_GLOW_PLUGS - temp) / TEMP_MINIMUM_FOR_GLOW_PLUGS);
  } else {
    self->glowPlugsTime = 0;
  }
}

/**
 * @brief Calculate dashboard lamp time from coolant temperature.
 * @param self Glow plug controller instance to update.
 * @param temp Coolant temperature used for the calculation.
 * @return None.
 */
static void glowPlugs_calculateGlowPlugLampTime(glowPlugs *self, float temp) {
  if (temp <= TEMP_VERY_LOW) {
    // Maximum time at very low temperature
    self->glowPlugsLampTime = MAX_LAMP_TIME;
  } else if (temp < TEMP_MINIMUM_FOR_GLOW_PLUGS) {
    // Proportional reduction in time within the range -20 to 10 degrees
    float tempDiff = TEMP_MINIMUM_FOR_GLOW_PLUGS - temp;
    float scaleFactor = tempDiff / (TEMP_MINIMUM_FOR_GLOW_PLUGS * 2);
    self->glowPlugsLampTime = MAX_LAMP_TIME * scaleFactor;
  } else {
    // Minimum lamp time for higher temperatures
    self->glowPlugsLampTime = MIN_LAMP_TIME;
  }

  // Ensure the lamp time is non-negative
  self->glowPlugsLampTime = (self->glowPlugsLampTime < 0) ? 0 : self->glowPlugsLampTime;
}

void glowPlugs_init(glowPlugs *self) {
  self->glowPlugsTime = 0;
  self->glowPlugsLampTime = 0;
  self->lastGlowPlugsTime = 0;
  self->lastGlowPlugsLampTime = 0;
  self->lastSecond = 0;
  self->warmAfterStart = false;
  self->initialized = false;
}

void glowPlugs_enableGlowPlugs(glowPlugs *self, bool enable) {
  (void)self;
  pcf8574_write(PCF8574_O_GLOW_PLUGS, enable);
}

void glowPlugs_glowPlugsLamp(glowPlugs *self, bool enable) {
  (void)self;
  pcf8574_write(PCF8574_O_GLOW_PLUGS_LAMP, enable);
}

bool glowPlugs_isGlowPlugsHeating(const glowPlugs *self) {
  return (self->glowPlugsTime > 0);
}

void glowPlugs_initGlowPlugsTime(glowPlugs *self, float temp) {

  glowPlugs_calculateGlowPlugsTime(self, temp);

  if(self->glowPlugsTime > 0) {
    glowPlugs_enableGlowPlugs(self, true);
    glowPlugs_glowPlugsLamp(self, true);

    glowPlugs_calculateGlowPlugLampTime(self, temp);

    self->lastSecond = getSeconds();
  }

  self->initialized = true;
}

void glowPlugs_process(glowPlugs *self) {

  if(!self->initialized) {
    return;
  }

  float temp = getGlobalValue(F_COOLANT_TEMP);
  if(temp > TEMP_COLD_ENGINE) {
    self->warmAfterStart = true;
  }

  if(!self->warmAfterStart) {
    if(temp <= TEMP_COLD_ENGINE &&
      getGlobalValue(F_RPM) > RPM_MIN) {
        glowPlugs_calculateGlowPlugsTime(self, temp);
        if(self->glowPlugsTime > 0) {
          glowPlugs_enableGlowPlugs(self, true);
          self->warmAfterStart = true;
        }
    }
  }

  if(self->glowPlugsTime >= 0 || self->glowPlugsLampTime >= 0) {
    bool pr = false;

    if(self->lastGlowPlugsTime != self->glowPlugsTime) {
      self->lastGlowPlugsTime = self->glowPlugsTime;
      pr = true;
    }
    if(self->lastGlowPlugsLampTime != self->glowPlugsLampTime) {
      self->lastGlowPlugsLampTime = self->glowPlugsLampTime;
      pr = true;
    }

    if(pr) {
      deb("glowPlugsTime: %d %d", self->glowPlugsTime, self->glowPlugsLampTime);
    }
  }

  if(self->glowPlugsTime >= 0) {
    if(getSeconds() != self->lastSecond) {
      self->lastSecond = getSeconds();

      if(self->glowPlugsTime-- <= 0) {
        glowPlugs_enableGlowPlugs(self, false);

        deb("glow plugs disabled");
      }

      if(self->glowPlugsLampTime >= 0 && self->glowPlugsLampTime-- <= 0) {
        glowPlugs_glowPlugsLamp(self, false);

        deb("glow plugs lamp off");
      }
    }
  }
}

void glowPlugs_showDebug(const glowPlugs *self) {
  deb("glowPlugs: %d", glowPlugs_isGlowPlugsHeating(self));
}
