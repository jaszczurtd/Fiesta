
#include "start.h"

static int readCycles = 0;
static int currentValue = 0;

static float valueFields[F_LAST];

void initialization(void) {
  for(int a = 0; a < F_LAST; a++) {
    valueFields[a] = 0.0;
  }

  initGraphics();
  redrawFuel();
  redrawTemperature();
  redrawOil();
  redrawPressure();
  redrawIntercooler();
  redrawEngineLoad();
  redrawRPM();
  redrawEGT();
}

void drawFunctions(void) {
  showFuelAmount((int)valueFields[F_FUEL], 1024);
  showTemperatureAmount((int)valueFields[F_COOLANT_TEMP], 120);
  showOilAmount((int)valueFields[F_OIL_TEMP], 150);
  showPressureAmount(valueFields[F_PRESSURE]);
  showICTemperatureAmount((unsigned char)valueFields[F_INTAKE_TEMP]);
  showEngineLoadAmount((unsigned char)valueFields[F_ENGINE_LOAD]);
  showRPMamount((int)valueFields[F_RPM]);
  showEGTTemperatureAmount((int)valueFields[F_EGT]);
}

void readValues(void) {
  if(readCycles++ > READ_CYCLES_AMOUNT) {
    readCycles = 0;

    switch(currentValue) {
      case F_COOLANT_TEMP:
        valueFields[F_COOLANT_TEMP] = readCoolantTemp();
        break;
      case F_OIL_TEMP:
        valueFields[F_OIL_TEMP] = readOilTemp();
        break;
    }
    if(currentValue++ > F_LAST) {
      currentValue = 0;
    }
  }
}

void seriousAlertsDrawFunctions() {
  drawFuelEmpty();

}

static bool draw = false, seriousAlertDraw = false;

static bool alertBlink = false, seriousAlertBlink = false;
bool alertSwitch(void) {
  return alertBlink;
}
bool seriousAlertSwitch(void) {
  return seriousAlertBlink;
}

static long lastSec = -1, lastHalfSec = -1;

void looper(void) {

  long msec = millis();

  int sec = (msec % 1000 > 500);
  int halfsec = (msec % 500 > 250);

  if(lastHalfSec != halfsec) {
    lastHalfSec = halfsec;
    seriousAlertBlink = (seriousAlertBlink) ? false : true;
    seriousAlertDraw = true;
  }

  if(lastSec != sec) {
    lastSec = sec;
    alertBlink = (alertBlink) ? false : true;
    draw = true;
  }

  if(draw) {
    drawFunctions();
    draw = false;
  }

  if(seriousAlertDraw) {
    seriousAlertsDrawFunctions();
    seriousAlertDraw = false;
  }

  readValues();

}


