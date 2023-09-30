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

    setDisplayDefaultFont();
    emptyMessageWidth = textWidth(emptyMessage);
    emptyMessageHeight = textHeight(emptyMessage);
}

float readFuel(void) {
    set4051ActivePin(HC4051_I_FUEL_LEVEL);

    int result = getAverageValueFrom(A1);
    int r = result;

    result -= FUEL_MAX;
    result = abs(result - (FUEL_MIN - FUEL_MAX));

    #ifdef DEBUG
    deb("tank raw value: %d result: %d", r, result);
    #endif

    #ifdef JUST_RAW_FUEL_VAL
    deb("tank raw:%d (%d)", r, result);
    lastResult = result;
    #else

    measuredValues[measuedValuesIndex] = result;
    measuedValuesIndex++;
    if(measuedValuesIndex > FUEL_MAX_SAMPLES) {
        measuedValuesIndex = 0;
    }

    int sec = getSeconds();
    if(lastResult == FUEL_INIT_VALUE) {
        nextMeasurement = sec - 1;
    }

    if(nextMeasurement < sec) {

        if(fuelMeasurementTime < FUEL_MEASUREMENT_TIME_DEST) {
            fuelMeasurementTime++;
        }
        nextMeasurement = sec + fuelMeasurementTime;

        long average = 0;
        int i; 
        for (i = 0; i < FUEL_MAX_SAMPLES; i++) {
            int v = measuredValues[i];
            if(v == FUEL_INIT_VALUE) {
                break;
            }
            average += v;
        }
        average /= i;

        deb("raw:%d (%d) num fuel samples: %d average val: %ld next probe time: %ds probes so far:%ld", 
            r, result, i, average, fuelMeasurementTime, ++measurements);

        lastResult = average;
    }
    #endif

    return lastResult;
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

    TFT tft = returnReference();
    tft.setFont();
    tft.setTextSize(1);    

    int x = f_getBaseX() + ((f_getWidth() - emptyMessageWidth) / 2);
    int y = f_getBaseY() + ((FUEL_HEIGHT - emptyMessageHeight) / 2);

    tft.setTextColor(color);
    tft.setCursor(x, y);
    tft.println(emptyMessage);
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

    TFT tft = returnReference();
    tft.fillRect(x, y, SCREEN_W, SCREEN_H - y, ICONS_BG_COLOR);

    x = f_getBaseX();

    drawImage(x - FUEL_WIDTH - OFFSET, y, FUEL_WIDTH, FUEL_HEIGHT, 0, (unsigned short*)fuelIcon);

    y = f_getGaugePos();

    tft.drawRect(x, y, width, FUEL_GAUGE_HEIGHT, FUEL_BOX_COLOR);

    drawChangeableFuelContent(currentFuelWidth, FUEL_GAUGE_HEIGHT, y);

    y += FUEL_GAUGE_HEIGHT + (OFFSET / 2);

    tft.setTextSize(1);
    tft.setTextColor(COLOR(RED));
    tft.setCursor(x, y);
    tft.println(empty);

    tw = textWidth(half);
    x = f_getBaseX();
    x += ((width - tw) / 2);

    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(x, y);
    tft.println(half);

    x = f_getBaseX() + width;
    tw = textWidth(full);
    x -= tw;

    tft.setCursor(x, y);
    tft.println(full);

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
      TFT tft = returnReference();
      int x = f_getBaseX(); 

      if(fullRedrawNeeded) {
          tft.fillRect(x + 1, y + 1, width - 2, fh - 2, COLOR(BLACK));
          fullRedrawNeeded = false;
      }

      tft.fillRect(x, y + 1, w, fh - 2, color);
      int toFill =  width - w - 1;
      if(toFill < 0) {
          toFill = 0;
      }
      tft.fillRect(x + w, y + 1, toFill, fh - 2, COLOR(BLACK));
      tft.drawRect(x, y, width, fh, FUEL_BOX_COLOR);
    }
}
