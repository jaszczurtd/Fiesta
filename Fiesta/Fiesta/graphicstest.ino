#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <Arduino.h>

#include "graphics.h"

#define TFT_CS     2
#define TFT_RST    0  // you can also connect this to the Arduino reset
                      // in which case, set this #define pin to 0!
#define TFT_DC     3

static char msg[128];

// OPTION 1 (recommended) is to use the HARDWARE SPI pins, which are unique
// to each board and not reassignable. For Arduino Uno: MOSI = pin 11 and
// SCLK = pin 13. This is the fastest mode of operation and is required if
// using the breakout board's microSD card.

// For 1.44" and 1.8" TFT with ST7735 use:
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

Adafruit_ST7735 returnReference(void) {
  return tft;
}

void setup(void) {
  Serial.begin(9600);

  // Use this initializer if using a 1.8" TFT screen:
  tft.initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
  tft.setRotation(1);

  Serial.println(F("Initialized"));

  tft.fillScreen(ST7735_WHITE);

  int x = (SCREEN_W - FIESTA_LOGO_WIDTH) / 2;
  int y = (SCREEN_H - FIESTA_LOGO_HEIGHT) / 2;
  drawImage(x, y, FIESTA_LOGO_WIDTH, FIESTA_LOGO_HEIGHT, 0xffff, (unsigned int*)FiestaLogo);
  delay(2000);
  tft.fillScreen(ST7735_BLACK);

  redrawFuel();
  redrawTemperature();
  redrawOil();
  redrawPressure();
}

void drawFunctions() {
  showFuelAmount(110, 1024);
  showTemperatureAmount(90, 120);
  showOilAmount(80, 150);
  showPressureAmount(1.0);
  

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

void loop() {

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


