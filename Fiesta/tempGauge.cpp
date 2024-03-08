
#include "tempGauge.h"

TempGauge coolant_g = TempGauge(TEMP_GAUGE_COOLANT);
TempGauge oil_g = TempGauge(TEMP_GAUGE_OIL);

void redrawTempGauges(void) {
  coolant_g.redraw();
  oil_g.redraw();
}

void showTempGauges(void) {
  coolant_g.showTemperatureAmount((int)valueFields[F_COOLANT_TEMP]);
  oil_g.showTemperatureAmount((int)valueFields[F_OIL_TEMP]);
}

TempGauge::TempGauge(int mode) {
  this->mode = mode;
  lastHeight = C_INIT_VAL;
  lastVal = C_INIT_VAL;
  lastFanImg = NULL;
  lastFanEnabled = false;
  drawOnce = true;
}

void TempGauge::drawTempBar(int x, int y, int currentHeight, int color) {
  TFT *tft = returnTFTReference();
  tft->fillRect(x, y, TEMP_BAR_WIDTH, -currentHeight, color);
  tft->fillRect(x, y - currentHeight, TEMP_BAR_WIDTH, 
      -(TEMP_BAR_MAXHEIGHT - currentHeight), ICONS_BG_COLOR);
}

void TempGauge::redraw(void) {
  drawOnce = true;
}

int TempGauge::getBaseX(void) {
  switch(mode) {
    case TEMP_GAUGE_COOLANT:
      return 0;
    case TEMP_GAUGE_OIL:
      return BIG_ICONS_WIDTH;
  }
  return -1;
}

int TempGauge::getBaseY(void) {
  switch(mode) {
    case TEMP_GAUGE_COOLANT:
    case TEMP_GAUGE_OIL:
      return BIG_ICONS_OFFSET; 
  }
  return -1;
}

void TempGauge::drawTempValue(int x, int y, int valToDisplay) {
  TFT *tft = returnTFTReference();
  tft->sansBoldWithPosAndColor(x, y, TEXT_COLOR);
  tft->fillRect(x, y - 14, 35, 16, ICONS_BG_COLOR);

  if(valToDisplay < TEMP_LOWEST || valToDisplay > TEMP_HIGHEST) {
      tft->setTextColor(COLOR(RED));
      tft->println(err);
      return;
  } else {
      char txt[DISPLAY_TXT_SIZE];
      tft->prepareText(txt, (const char*)F("%d"), valToDisplay);
      tft->printlnFromPreparedText(txt);
  }
  tft->setDisplayDefaultFont();
}

int TempGauge::currentValToHeight(int currentVal, int maxVal) {
  float percent = (float)(currentVal * 100) / (float)maxVal;
  return percentToGivenVal(percent, TEMP_BAR_MAXHEIGHT);
}

void TempGauge::showTemperatureAmount(int currentVal) {

  TFT *tft = returnTFTReference();
  unsigned short *tempImg = NULL;

  if(drawOnce) {
    switch(mode) {
      case TEMP_GAUGE_COOLANT:
        tempImg = (unsigned short*)temperature;
        break;
      case TEMP_GAUGE_OIL:
        tempImg = (unsigned short*)oil;
        break;        
    }

    tft->drawImage(getBaseX(), getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, ICONS_BG_COLOR, tempImg);
    drawOnce = false;
  } else {
    
    int maxVal = 0;
    int x = 0, y = 0, color;

    int valToDisplay = currentVal;
    int temp_max = 0;
    int temp_ok_hi = 0;

    switch(mode) {
      case TEMP_GAUGE_COOLANT:
        maxVal = TEMP_MAX;
        temp_max = TEMP_MAX;
        temp_ok_hi = TEMP_OK_HI;
        break;
      case TEMP_GAUGE_OIL:
        maxVal = TEMP_OIL_MAX;
        temp_max = TEMP_OIL_MAX;
        temp_ok_hi = TEMP_OIL_OK_HI;
        break;
    }

    if(currentVal > temp_max) {
        currentVal = temp_max;
    }

    bool overheat = false;
    color = TEMP_INITIAL_COLOR;
    if(currentVal >= TEMP_OK_LO && currentVal <= temp_ok_hi) {
        color = COLOR(ORANGE);
    } 
    if(currentVal > temp_ok_hi) {
        overheat = true;
    }

    bool blink = alertSwitch();
    if(currentVal > temp_ok_hi + ((temp_max - temp_ok_hi) / 2)) {
      blink = seriousAlertSwitch();
    }

    currentVal -= TEMP_MIN;
    maxVal -= TEMP_MIN;
    if(currentVal < 0) {
        currentVal = 0;
    }

    int currentHeight = currentValToHeight(
        (currentVal < temp_max) ? currentVal : temp_max,
        maxVal);

    bool draw = false;
    if(lastHeight != currentHeight) {
        lastHeight = currentHeight;
        draw = true;
    }

    if(overheat) {
        draw = true;
        color = (blink) ? COLOR(RED) : COLOR(ORANGE);
    }

    bool fanEnabled = isFanEnabled();
    unsigned short *img = NULL;

    switch(mode) {
      case TEMP_GAUGE_COOLANT:
        if(fanEnabled) {
          if(seriousAlertSwitch()) {
            img = (unsigned short*)fan_b_Icon;
          } else {
            img = (unsigned short*)fan_a_Icon;
          }

          if(img != lastFanImg) {
            lastFanImg = img;
            draw = true;
          }
        }
        if(lastFanEnabled != fanEnabled) {
          lastFanEnabled = fanEnabled;
          draw = true;
        }
        break;
    }

    if(draw) {
      switch(mode) {
        case TEMP_GAUGE_COOLANT:
          x = getBaseX() + 24;
          break;
        case TEMP_GAUGE_OIL:
          x = getBaseX() + 20;
          break;
      }

      y = getBaseY() + 8 + TEMP_BAR_MAXHEIGHT;
      drawTempBar(x, y, currentHeight, color);

      switch(mode) {
        case TEMP_GAUGE_COOLANT:
          if(fanEnabled && img != NULL) {
            x = getBaseX() + FAN_COOLANT_X;
            y = getBaseY() + FAN_COOLANT_Y;

            tft->drawRGBBitmap(x, y, img, FAN_COOLANT_WIDTH, FAN_COOLANT_HEIGHT);
          } else {
            x = getBaseX() + TEMP_DOT_X;
            y = getBaseY() + TEMP_DOT_Y;

            tft->fillCircle(x, y, TEMP_BAR_DOT_RADIUS, color);
          }
          break;
        case TEMP_GAUGE_OIL:
          x = getBaseX() + OIL_DOT_X;
          y = getBaseY() + OIL_DOT_Y;

          tft->fillCircle(x, y, TEMP_BAR_DOT_RADIUS, color);
          break;
      }
    }

    if(lastVal != valToDisplay) {
      lastVal = valToDisplay;

      switch(mode) {
        case TEMP_GAUGE_COOLANT:
          x = getBaseX() + 40;
          break;
        case TEMP_GAUGE_OIL:
          x = getBaseX() + 36;
          break;
      }
      y = getBaseY() + 38;
      drawTempValue(x, y, valToDisplay);
    }
  }
}
