#include "engineHeater.h"

//-----------------------------------------------------------------------------
// engine heater
//-----------------------------------------------------------------------------

static engineHeater *heat = nullptr;
void createHeater(void) {
  heat = new engineHeater();
  heat->init();
}

engineHeater *getHeaterInstance(void) {
  if(heat == nullptr) {
    createHeater();
  }
  return heat;
}

engineHeater::engineHeater() { }

void engineHeater::init() {
  heaterLoEnabled = heaterHiEnabled= lastHeaterLoEnabled = lastHeaterHiEnabled = false;
}

void engineHeater::heater(bool enable, int level) {
  pcf8574_write(level, enable);
}

void engineHeater::process(void) {
  float coolant = valueFields[F_COOLANT_TEMP];
  float volts = valueFields[F_VOLTS];
  int engineRPM = valueFields[F_RPM];

  if(coolant > TEMP_HEATER_STOP ||
    getFanInstance()->isFanEnabled() ||
    getGlowPlugsInstance()->isGlowPlugsHeating() ||
    volts < MINIMUM_VOLTS_AMOUNT ||
    engineRPM < RPM_MIN) {
    heaterLoEnabled = false;
    heaterHiEnabled = false;
  } else {

    if(coolant <= int(float(TEMP_HEATER_STOP) / 1.5)) {
      heaterLoEnabled = heaterHiEnabled = true;
    } else {
      heaterLoEnabled = true;
      heaterHiEnabled = false;
    }

  }

  if(lastHeaterHiEnabled != heaterHiEnabled) {
    heater(heaterHiEnabled, PCF8574_O_HEATER_HI);
    lastHeaterHiEnabled = heaterHiEnabled;
  }
  if(lastHeaterLoEnabled != heaterLoEnabled) {
    heater(heaterLoEnabled, PCF8574_O_HEATER_LO);
    lastHeaterLoEnabled = heaterLoEnabled;
  }

}

void engineHeater::showDebug() {
  deb("heaterStatus: loEnabled:%d hiEnabled:%d", heaterLoEnabled, heaterHiEnabled);
}

