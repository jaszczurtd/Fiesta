#include "engineFuel.h"

//-------------------------------------------------------------------------------------------------
//Read fuel amount
//-------------------------------------------------------------------------------------------------

EngineFuelGauge fuel_g = EngineFuelGauge();

void redrawFuel(void) {
  fuel_g.redraw();
}

void drawFuelEmpty(void) {
  fuel_g.drawFuelEmpty();
}

void showFuelAmount(void) {
  fuel_g.showFuelAmount((int)valueFields[F_FUEL], FUEL_MIN - FUEL_MAX);
}

void initFuelMeasurement(void) {
  fuel_g.init();
}

EngineFuelGauge::EngineFuelGauge(void) { }

void EngineFuelGauge::init() {
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

  currentFuelWidth = 0;
  fullRedrawNeeded = false;
  lastWidth = 0;
}

//-------------------------------------------------------------------------------------------------
//fuel indicator
//-------------------------------------------------------------------------------------------------

void EngineFuelGauge::redraw(void) {
    f_drawOnce = true;
}

int EngineFuelGauge::getBaseX(void) {
    return (OFFSET * 2) + FUEL_WIDTH + 4;
}

int EngineFuelGauge::getBaseY(void) {
    return SCREEN_H - FUEL_HEIGHT - 8 - (OFFSET * 2); 
}

int EngineFuelGauge::getWidth(void) {
    return FUEL_GAUGE_WIDTH - getBaseX();
}

int EngineFuelGauge::getGaugePos(void) {
  int center = ((FUEL_HEIGHT - FUEL_GAUGE_HEIGHT) / 2);
  if(center < 0) {
    center = 0;
  }
  return getBaseY() + center;
}

void EngineFuelGauge::drawFuelEmpty(void) {

  if(lastResult == FUEL_INIT_VALUE) {
    return;
  }

  if(currentFuelWidth <= MIN_FUEL_WIDTH) {

    int color = COLOR(WHITE);
    if(seriousAlertSwitch()) {
        color = COLOR(RED);
    }

    int x = getBaseX() + ((getWidth() - emptyMessageWidth) / 2);
    int y = getBaseY() + ((FUEL_HEIGHT - emptyMessageHeight) / 2);

    TFT *tft = returnTFTReference();
    tft->defaultFontWithPosAndColor(x, y, color);
    tft->println(emptyMessage);
  }
}

void EngineFuelGauge::showFuelAmount(int currentVal, int maxVal) {
  int width = getWidth();
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
    int y = getBaseY();
    int tw;

    TFT *tft = returnTFTReference();
    tft->fillRect(FUEL_WIDTH + OFFSET + (OFFSET / 2),
                  y, 
                  getWidth() + OFFSET, SCREEN_H - y, 
                  ICONS_BG_COLOR);

    x = getBaseX();

    tft->drawImage(x - FUEL_WIDTH - OFFSET, y, FUEL_WIDTH, FUEL_HEIGHT, 0, (unsigned short*)fuelIcon);

    y = getGaugePos();

    tft->drawRect(x, y, width, FUEL_GAUGE_HEIGHT, FUEL_BOX_COLOR);

    drawChangeableFuelContent(currentFuelWidth, FUEL_GAUGE_HEIGHT, y);

    y += FUEL_GAUGE_HEIGHT + (OFFSET / 2);

    tft->defaultFontWithPosAndColor(x, y, COLOR(RED));
    tft->println(empty);

    tw = tft->textWidth(half);
    x = getBaseX();
    x += ((width - tw) / 2);

    tft->setTextColor(TEXT_COLOR);
    tft->setCursor(x, y);
    tft->println(half);

    x = getBaseX() + width;
    tw = tft->textWidth(full);
    x -= tw;

    tft->setCursor(x, y);
    tft->println(full);

    f_drawOnce = false;
  } else {
    drawChangeableFuelContent(currentFuelWidth, FUEL_GAUGE_HEIGHT, getGaugePos());
  }
  drawFuelEmpty();
}

void EngineFuelGauge::drawChangeableFuelContent(int w, int fh, int y) {

    bool draw = false;
    if(lastWidth != w) {
        lastWidth = w;
        draw = true;
    }

    int color = FUEL_FILL_COLOR;

    int width = getWidth();
    int minW = percentToGivenVal(MINIMUM_FUEL_AMOUNT_PERCENTAGE, width);
    if(w <= minW && w >= 1) {
        draw = true;
        if(alertSwitch()) {
            color = COLOR(RED);
        }
    }

    if(draw) {
      TFT *tft = returnTFTReference();
      int x = getBaseX(); 

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
