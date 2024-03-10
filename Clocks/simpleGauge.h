
#ifndef T_SIMPLE_GAUGE
#define T_SIMPLE_GAUGE

#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <tools.h>

#include "TFTExtension.h"
#include "config.h"
#include "icons.h"

enum {
  SIMPLE_G_NONE,
  SIMPLE_G_ENGINE_LOAD,
  SIMPLE_G_INTAKE,
  SIMPLE_G_RPM,
  SIMPLE_G_GPS,
  SIMPLE_G_EGT,
  SIMPLE_G_VOLTS,
  SIMPLE_G_ECU
};

//text modes
enum {
  MODE_M_NORMAL,
  MODE_M_TEMP,
  MODE_M_KILOMETERS
};

#define ECU_CONNECTION_RADIUS 4

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
void showECUConnectionGauge(void);

#endif
