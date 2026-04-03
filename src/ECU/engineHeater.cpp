#include "engineHeater.h"
#include "ecuContext.h"

//-----------------------------------------------------------------------------
// engine heater
//-----------------------------------------------------------------------------

void createHeater(void) {
  engineHeater_init(getHeaterInstance());
}

engineHeater *getHeaterInstance(void) {
  return &getECUContext()->heater;
}

void engineHeater_init(engineHeater *self) {
  self->heaterLoEnabled = self->heaterHiEnabled =
    self->lastHeaterLoEnabled = self->lastHeaterHiEnabled = false;
}

void engineHeater_heater(engineHeater *self, bool enable, int level) {
  (void)self;
  pcf8574_write(level, enable);
}

void engineHeater_process(engineHeater *self) {
  float coolant = getGlobalValue(F_COOLANT_TEMP);
  float volts = getGlobalValue(F_VOLTS);
  int engineRPM = getGlobalValue(F_RPM);

  if(coolant > TEMP_HEATER_STOP ||
    engineFan_isFanEnabled(getFanInstance()) ||
    glowPlugs_isGlowPlugsHeating(getGlowPlugsInstance()) ||
    volts < MINIMUM_VOLTS_AMOUNT ||
    engineRPM < RPM_MIN) {
    self->heaterLoEnabled = false;
    self->heaterHiEnabled = false;
  } else {

    if(coolant <= (int)((float)(TEMP_HEATER_STOP) / 1.5f)) {
      self->heaterLoEnabled = self->heaterHiEnabled = true;
    } else {
      self->heaterLoEnabled = true;
      self->heaterHiEnabled = false;
    }

  }

  if(self->lastHeaterHiEnabled != self->heaterHiEnabled) {
    engineHeater_heater(self, self->heaterHiEnabled, PCF8574_O_HEATER_HI);
    self->lastHeaterHiEnabled = self->heaterHiEnabled;
  }
  if(self->lastHeaterLoEnabled != self->heaterLoEnabled) {
    engineHeater_heater(self, self->heaterLoEnabled, PCF8574_O_HEATER_LO);
    self->lastHeaterLoEnabled = self->heaterLoEnabled;
  }

}

void engineHeater_showDebug(engineHeater *self) {
  deb("heaterStatus: loEnabled:%d hiEnabled:%d", self->heaterLoEnabled, self->heaterHiEnabled);
}
