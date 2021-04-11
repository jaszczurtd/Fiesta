
#include "start.h"

void initialization(void) {
    initGraphics();
    redrawFuel();
    redrawTemperature();
    redrawOil();
    redrawPressure();
    redrawIntercooler();
    redrawEngineLoad();
}

void drawFunctions() {
  showFuelAmount(110, 1024);
  showTemperatureAmount(120, 120);
  showOilAmount(150, 150);
  showPressureAmount(1.0);
  showICTemperatureAmount(25);
  showEngineLoadAmount(40);

/*  
  int c[] = {
    ST7735_CYAN,
    ST7735_GREEN,
    ST7735_YELLOW,
    ST7735_RED
  };

  int x = 0;
  for(int a = 0; a < 4; a++) {
    tft.fillRect(x, 53, 40, 40, c[a]);

    x += 40;
  } 
  */

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

}


