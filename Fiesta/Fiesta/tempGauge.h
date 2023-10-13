
#ifndef T_TEMP_GAUGE
#define T_TEMP_GAUGE

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <tools.h>

#include "TFTExtension.h"
#include "config.h"
#include "icons.h"
#include "graphics.h"

#define TEMP_GAUGE_NONE     0
#define TEMP_GAUGE_OIL      1
#define TEMP_GAUGE_COOLANT  2

#define TEMP_BAR_MAXHEIGHT 40
#define TEMP_BAR_WIDTH 6
#define TEMP_BAR_DOT_RADIUS 11

class TempGauge {
public:
  TempGauge(int mode, TFT& tft);
  void drawTempBar(int x, int y, int currentHeight, int color);

private:
  TFT& tft;
  int mode;
};

#endif
