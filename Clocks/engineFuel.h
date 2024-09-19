#ifndef T_FUEL
#define T_FUEL

#include <tools.h>

#include "config.h"
#include "logic.h"
#include "TFTExtension.h"

//fuel value read without average calculation
//#define JUST_RAW_FUEL_VAL

#define FUEL_MAX_SAMPLES 128
#define FUEL_INIT_VALUE -1

#define FUEL_MEASUREMENT_TIME_START 5
#define FUEL_MEASUREMENT_TIME_DEST 30

#define FUEL_WIDTH 30
#define FUEL_HEIGHT 30

#define FUEL_GAUGE_HEIGHT (FUEL_HEIGHT - 4)
#define FUEL_GAUGE_WIDTH (SCREEN_W - 118)

#define MIN_FUEL_WIDTH 2

#define FUEL_COLOR 0xfda0

#define FUEL_BOX_COLOR 0xBDF7
#define FUEL_FILL_COLOR 0x9CD3

class EngineFuelGauge : public Gauge {
public:
  EngineFuelGauge(void);
  void redraw(void);
  int getBaseX(void);
  int getBaseY(void);
  int getWidth(void);
  int getGaugePos(void);
  void drawFuelEmpty(void);
  void showFuelAmount(int currentVal, int maxVal);
  void drawChangeableFuelContent(int w, int fh, int y);

private:
  bool f_drawOnce; 

  const char *half = (char*)F("1/2");
  const char *full = (char*)F("F");
  const char *empty = (char*)F("E");
  const char *emptyMessage = (char*)F("Empty tank!");

  int measuredValues[FUEL_MAX_SAMPLES];
  int measuedValuesIndex;
  int lastResult;
  int nextMeasurement;
  int fuelMeasurementTime;
  long measurements;

  int emptyMessageWidth;
  int emptyMessageHeight;

  int currentFuelWidth;
  bool fullRedrawNeeded;
  int lastWidth;
};

void redrawFuel(void);
void drawFuelEmpty(void);
void showFuelAmount(void);


#endif