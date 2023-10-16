
#ifndef T_SIMPLE_GAUGE
#define T_SIMPLE_GAUGE

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <tools.h>

#include "TFTExtension.h"
#include "config.h"
#include "icons.h"
#include "graphics.h"

#define SIMPLE_G_NONE 0
#define SIMPLE_G_ENGINE_LOAD 1

class SimpleGauge {
public:
  SimpleGauge(int mode);
  void redraw(void);
  int getBaseX(void);
  int getBaseY(void);
  void showSimpleGauge(void);

private:
  bool drawOnce;
  int mode;
  int lastLoadAmount;
};

void redrawSimpleGauges(void);
void showEngineLoadGauge(void);

#endif
