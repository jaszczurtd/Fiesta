
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

#define TEMP_DOT_X 26
#define TEMP_DOT_Y 59

#define OIL_DOT_X 22
#define OIL_DOT_Y 59

#define FAN_COOLANT_X 15
#define FAN_COOLANT_Y 48

#define OFFSET (SCREEN_W / 40)

extern const char *err;

void redrawTempGauges(void);
void showTempGauges(void);

class TempGauge {
public:
  TempGauge(int mode);
  void drawTempValue(int x, int y, int valToDisplay);
  int currentValToHeight(int currentVal, int maxVal);
  void drawTempBar(int x, int y, int currentHeight, int color);
  void redraw(void);
  int getBaseX(void);
  int getBaseY(void);
  void showTemperatureAmount(int currentVal);

private:
  bool drawOnce;
  int lastHeight;
  int lastVal;
  unsigned short *lastFanImg;
  bool lastFanEnabled;
  int mode;
};

#endif
