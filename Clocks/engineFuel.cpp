#include "engineFuel.h"

const char *half = (char*)F("1/2");
const char *full = (char*)F("F");
const char *empty = (char*)F("E");
const char *emptyMessage = (char*)F("Empty tank!");

//-------------------------------------------------------------------------------------------------
//Read fuel amount
//-------------------------------------------------------------------------------------------------

static int measuredValues[FUEL_MAX_SAMPLES];
static int measuedValuesIndex = 0;
static int lastResult = FUEL_INIT_VALUE;
static int nextMeasurement = 0;
static int fuelMeasurementTime = 0;
static long measurements = 0;

static int emptyMessageWidth;
static int emptyMessageHeight;

void initFuelMeasurement(void) {
  memset(measuredValues, FUEL_INIT_VALUE, sizeof(measuredValues));
  measuedValuesIndex = 0;
  lastResult = FUEL_INIT_VALUE;

  fuelMeasurementTime = FUEL_MEASUREMENT_TIME_START;
  nextMeasurement = getSeconds() + fuelMeasurementTime;
  measurements = 0;

  TFT *tft = returnTFTReference();

  tft->setDisplayDefaultFont();
  emptyMessageWidth = tft->textWidth(emptyMessage);
  emptyMessageHeight = tft->textHeight(emptyMessage);
}

//-------------------------------------------------------------------------------------------------
//fuel indicator
//-------------------------------------------------------------------------------------------------

static bool f_drawOnce = true; 
void redrawFuel(void) {
    f_drawOnce = true;
}

static int currentFuelWidth = 0;
static bool fullRedrawNeeded = false;

int f_getBaseX(void) {
    return (OFFSET * 2) + FUEL_WIDTH - 1;
}

int f_getBaseY(void) {
    return SCREEN_H - FUEL_HEIGHT - 8 - (OFFSET * 2); 
}

int f_getWidth(void) {
    return FUEL_GAUGE_WIDTH - f_getBaseX();
}

int f_getGaugePos(void) {
  int center = ((FUEL_HEIGHT - FUEL_GAUGE_HEIGHT) / 2);
  if(center < 0) {
    center = 0;
  }
  return f_getBaseY() + center;
}

void drawFuelEmpty(void) {

  if(lastResult == FUEL_INIT_VALUE) {
    return;
  }

  if(currentFuelWidth <= MIN_FUEL_WIDTH) {

    int color = COLOR(WHITE);
    if(seriousAlertSwitch()) {
        color = COLOR(RED);
    }

    int x = f_getBaseX() + ((f_getWidth() - emptyMessageWidth) / 2);
    int y = f_getBaseY() + ((FUEL_HEIGHT - emptyMessageHeight) / 2);

    TFT *tft = returnTFTReference();
    tft->defaultFontWithPosAndColor(x, y, color);
    tft->println(emptyMessage);
  }
}

void showFuelAmount(int currentVal, int maxVal) {
  int width = f_getWidth();
  float percent = (currentVal * 100) / maxVal;
  currentFuelWidth = percentToGivenVal(percent, width);
  if(currentFuelWidth > width) {
      currentFuelWidth = width;
  }

  if(currentFuelWidth <= MIN_FUEL_WIDTH && !fullRedrawNeeded) {
      fullRedrawNeeded = true;
  }

  int center = ((FUEL_HEIGHT - FUEL_GAUGE_HEIGHT) / 2);
  if(center < 0) {
    center = 0;
  }

  if(f_drawOnce) {
    int x = 0; 
    int y = f_getBaseY();
    int tw;

    TFT *tft = returnTFTReference();
    tft->fillRect(FUEL_WIDTH + OFFSET + (OFFSET / 2),
                  y, 
                  f_getWidth() + OFFSET, SCREEN_H - y, 
                  ICONS_BG_COLOR);

    x = f_getBaseX();

    tft->drawImage(x - FUEL_WIDTH - OFFSET, y, FUEL_WIDTH, FUEL_HEIGHT, 0, (unsigned short*)fuelIcon);

    y = f_getGaugePos();

    tft->drawRect(x, y, width, FUEL_GAUGE_HEIGHT, FUEL_BOX_COLOR);

    drawChangeableFuelContent(currentFuelWidth, FUEL_GAUGE_HEIGHT, y);

    y += FUEL_GAUGE_HEIGHT + (OFFSET / 2);

    tft->defaultFontWithPosAndColor(x, y, COLOR(RED));
    tft->println(empty);

    tw = tft->textWidth(half);
    x = f_getBaseX();
    x += ((width - tw) / 2);

    tft->setTextColor(TEXT_COLOR);
    tft->setCursor(x, y);
    tft->println(half);

    x = f_getBaseX() + width;
    tw = tft->textWidth(full);
    x -= tw;

    tft->setCursor(x, y);
    tft->println(full);

    f_drawOnce = false;
  } else {
    drawChangeableFuelContent(currentFuelWidth, FUEL_GAUGE_HEIGHT, f_getGaugePos());
  }
  drawFuelEmpty();
}

static int lastWidth = 0;

void drawChangeableFuelContent(int w, int fh, int y) {

    bool draw = false;
    if(lastWidth != w) {
        lastWidth = w;
        draw = true;
    }

    int color = FUEL_FILL_COLOR;

    int width = f_getWidth();
    int minW = percentToGivenVal(MINIMUM_FUEL_AMOUNT_PERCENTAGE, width);
    if(w <= minW && w >= 1) {
        draw = true;
        if(alertSwitch()) {
            color = COLOR(RED);
        }
    }

    if(draw) {
      TFT *tft = returnTFTReference();
      int x = f_getBaseX(); 

      if(fullRedrawNeeded) {
          tft->fillRect(x + 1, y + 1, width - 2, fh - 2, COLOR(BLACK));
          fullRedrawNeeded = false;
      }

      tft->fillRect(x, y + 1, w, fh - 2, color);
      int toFill =  width - w - 1;
      if(toFill < 0) {
          toFill = 0;
      }
      tft->fillRect(x + w, y + 1, toFill, fh - 2, COLOR(BLACK));
      tft->drawRect(x, y, width, fh, FUEL_BOX_COLOR);
    }
}
