
#ifndef T_PRESSURE_GAUGE
#define T_PRESSURE_GAUGE

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <tools.h>

#include "TFTExtension.h"
#include "config.h"
#include "icons.h"

#define PRESSURE_G_NONE 0
#define PRESSURE_G_TURBO 1
#define PRESSURE_G_OIL 2

#define TURPO_PERCENT_TEXT_POS_X 12
#define TURPO_PERCENT_TEXT_POS_Y 76

extern const char *err;

class PressureGauge {
public:
  PressureGauge(int mode);
  void redraw(void);
  int getBaseX(void);
  int getBaseY(void);
  void showPressureGauge(void);
  void showPressurePercentage(void);

private:
  int mode;
  bool drawOnce;
  int lastShowedVal;
  int lastHI;
  int lastLO;
  int lastHI_d;
  int lastLO_d;
  unsigned short *lastAnimImg; 
};

void redrawPressureGauges(void);
void showPressureGauges(void);

#endif
