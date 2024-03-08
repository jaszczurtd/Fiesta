
#ifndef T_SIMPLE_GAUGE
#define T_SIMPLE_GAUGE

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <tools.h>

#include "TFTExtension.h"
#include "config.h"
#include "icons.h"

#define SIMPLE_G_NONE 0
#define SIMPLE_G_ENGINE_LOAD 1
#define SIMPLE_G_INTAKE 2
#define SIMPLE_G_RPM 3
#define SIMPLE_G_GPS 4
#define SIMPLE_G_EGT 5
#define SIMPLE_G_VOLTS 6

//text modes
#define MODE_M_NORMAL 0
#define MODE_M_TEMP 1
#define MODE_M_KILOMETERS 2

#define VOLTS_OK_COLOR 0x4228
#define VOLTS_LOW_ERROR_COLOR 0xA000

extern const char *err;

class SimpleGauge {
public:
  SimpleGauge(int mode);
  int drawTextForMiddleIcons(int x, int y, int offset, int color, int mode, const char *format, ...);
  void redraw(void);
  void switchCurrentEGTMode(void);
  void resetCurrentEGTMode(void);
  int getBaseX(void);
  int getBaseY(void);
  void showSimpleGauge(void);

private:
  int mode;
  bool drawOnce;
  int lastShowedVal;
  bool currentIsDPF;
  int lastV1;
  int lastV2;
  char displayTxt[16];
};

void redrawSimpleGauges(void);
void showEngineLoadGauge(void);
void showGPSGauge(void);
void showSimpleGauges(void);
void showEGTGauge(void);
bool changeEGT(void *argument);

#endif
