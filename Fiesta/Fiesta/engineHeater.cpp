#include "engineHeater.h"

//-----------------------------------------------------------------------------
// engine heater
//-----------------------------------------------------------------------------

void heater(bool enable, int level) {
  pcf8574_write(level, enable);
}

static bool heaterLoEnabled = false;
static bool heaterHiEnabled = false;
static bool lastHeaterLoEnabled = false;
static bool lastHeaterHiEnabled = false;

void engineHeaterMainLoop(void) {
  float coolant = valueFields[F_COOLANT_TEMP];
  float volts = valueFields[F_VOLTS];
  int engineRPM = valueFields[F_RPM];

  if(coolant > TEMP_HEATER_STOP ||
    isFanEnabled() ||
    isGlowPlugsHeating() ||
    volts < MINIMUM_VOLTS_AMOUNT ||
    engineRPM < RPM_MIN) {
    heaterLoEnabled = false;
    heaterHiEnabled = false;
  } else {

    if(coolant <= (TEMP_HEATER_STOP / 2)) {
      heaterLoEnabled = heaterHiEnabled = true;
    } else {
      heaterLoEnabled = true;
      heaterHiEnabled = false;
    }

  }

  if(lastHeaterHiEnabled != heaterHiEnabled) {
    heater(heaterHiEnabled, O_HEATER_HI);
    lastHeaterHiEnabled = heaterHiEnabled;
  }
  if(lastHeaterLoEnabled != heaterLoEnabled) {
    heater(heaterLoEnabled, O_HEATER_LO);
    lastHeaterLoEnabled = heaterLoEnabled;
  }

}

