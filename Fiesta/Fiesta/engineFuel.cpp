#include "engineFuel.h"

//-------------------------------------------------------------------------------------------------
//Read fuel amount
//-------------------------------------------------------------------------------------------------

static int measuredValues[FUEL_MAX_SAMPLES];
static int measuedValuesIndex = 0;
static int lastResult = FUEL_INIT_VALUE;
static int nextMeasurement = 0;
static int fuelMeasurementTime = 0;
static long measurements = 0;

void initFuelMeasurement(void) {
    memset(measuredValues, FUEL_INIT_VALUE, sizeof(measuredValues));
    measuedValuesIndex = 0;
    lastResult = FUEL_INIT_VALUE;

    fuelMeasurementTime = FUEL_MEASUREMENT_TIME_START;
    nextMeasurement = getSeconds() + fuelMeasurementTime;
    measurements = 0;
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

        deb("raw:%d (%d) num fuel samples: %d average val: %ld next probe time: %ds probes so far:%ld", r, result, i, average, fuelMeasurementTime, ++measurements);

        lastResult = average;
    }

    return lastResult;
}

//-------------------------------------------------------------------------------------------------
//fuel indicator
//-------------------------------------------------------------------------------------------------

const char *half = (char*)F("1/2");
const char *full = (char*)F("F");
const char *empty = (char*)F("E");
const char *emptyMessage = (char*)F("Pusty bak!");

static bool f_drawOnce = true; 
void redrawFuel(void) {
    f_drawOnce = true;
}

static int currentFuelWidth = 0;
static bool fullRedrawNeeded = false;

int f_getBaseX(void) {
    return (OFFSET * 2) + FUEL_WIDTH + OFFSET;
}

int f_getBaseY(void) {
    return SCREEN_H - FUEL_HEIGHT - textHeight(empty) - OFFSET; 
}

int f_getWidth(void) {
    return SCREEN_W - f_getBaseX() - (OFFSET * 2);
}

void drawFuelEmpty(void) {
    if(currentFuelWidth <= 1) {

        int color = COLOR(WHITE);
        if(seriousAlertSwitch()) {
            color = COLOR(RED);
        }

        int x = f_getBaseX() + ((f_getWidth() - textWidth(emptyMessage)) / 2);
        int y = f_getBaseY() + ((FUEL_HEIGHT - textHeight(emptyMessage)) / 2);

        TFT tft = returnReference();
        tft.setTextSize(1);
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
    if(currentFuelWidth <= 1 && !fullRedrawNeeded) {
        fullRedrawNeeded = true;
    }

    if(f_drawOnce) {
        int x = 0; 
        int y = y = f_getBaseY();
        int tw;

        TFT tft = returnReference();
        tft.fillRect(x, y, SCREEN_W, SCREEN_H - y, COLOR(BLACK));

        x = f_getBaseX();

        drawImage(x - FUEL_WIDTH - OFFSET, y, FUEL_WIDTH, FUEL_HEIGHT, 0, (unsigned short*)fuelIcon);
        tft.drawRect(x, y, width, FUEL_HEIGHT, FUEL_BOX_COLOR);

        tft.fillTriangle(x - 6 - FUEL_WIDTH - OFFSET, 
            y + ((FUEL_HEIGHT) / 2), 
            x - FUEL_WIDTH - OFFSET - 1, 
            y + 6, 
            x - FUEL_WIDTH - OFFSET - 1, 
            y + (FUEL_HEIGHT - 6), 
            FUEL_COLOR);

        drawChangeableFuelContent(currentFuelWidth);

        y += FUEL_HEIGHT + (OFFSET / 2);

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
        drawChangeableFuelContent(currentFuelWidth);
    }
}

static int lastWidth = 0;

void drawChangeableFuelContent(int w) {

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
      int x = f_getBaseX(), y = f_getBaseY(); 

      if(fullRedrawNeeded) {
          tft.fillRect(x + 1, y + 1, width - 2, FUEL_HEIGHT - 2, COLOR(BLACK));
          fullRedrawNeeded = false;
      }

      tft.fillRect(x, y + 1, w, FUEL_HEIGHT - 2, color);
      int toFill =  width - w - 1;
      if(toFill < 0) {
          toFill = 0;
      }
      tft.fillRect(x + w, y + 1, toFill, FUEL_HEIGHT - 2, COLOR(BLACK));
      tft.drawRect(x, y, width, FUEL_HEIGHT, FUEL_BOX_COLOR);
    }
}
