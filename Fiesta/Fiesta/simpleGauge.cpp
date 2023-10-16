
#include "simpleGauge.h"

SimpleGauge engineLoad_g = SimpleGauge(SIMPLE_G_ENGINE_LOAD);
SimpleGauge intake_g = SimpleGauge(SIMPLE_G_INTAKE);
SimpleGauge rpm_g = SimpleGauge(SIMPLE_G_RPM);
SimpleGauge gps_g = SimpleGauge(SIMPLE_G_GPS);
SimpleGauge egt_g = SimpleGauge(SIMPLE_G_EGT);

void redrawSimpleGauges(void) {
  engineLoad_g.redraw();
  intake_g.redraw();
  rpm_g.redraw();
  gps_g.redraw();
  egt_g.redraw();
}

void showEngineLoadGauge(void) {
  engineLoad_g.showSimpleGauge();
}

void showGPSGauge(void) {
  gps_g.showSimpleGauge();
}

void showSimpleGauges(void) {
  intake_g.showSimpleGauge();
  rpm_g.showSimpleGauge();
}

void showEGTGauge(void) {
  egt_g.showSimpleGauge();
}

bool changeEGT(void *argument) {
  if(isDPFConnected()) {
    egt_g.switchCurrentEGTMode();
  } else {
    egt_g.resetCurrentEGTMode();
  }

  return true;
}

SimpleGauge::SimpleGauge(int mode) {
  this->mode = mode;
  drawOnce = true;
  lastShowedVal = C_INIT_VAL;
  resetCurrentEGTMode();
}

int SimpleGauge::drawTextForMiddleIcons(int x, int y, int offset, int color, int mode, const char *format, ...) {

  TFT tft = returnTFTReference();

  int w1 = 0, kmoffset = 0;
  const char *km = ((const char*)F("km/h"));
  if(mode == MODE_M_KILOMETERS) {
    tft.setDisplayDefaultFont();
    w1 = tft.textWidth(km);
    kmoffset = 5;
  }
  tft.serif9ptWithColor(color);

  memset(displayTxt, 0, sizeof(displayTxt));

  va_list valist;
  va_start(valist, format);
  vsnprintf(displayTxt, sizeof(displayTxt) - 1, format, valist);
  va_end(valist);

  int w = tft.textWidth((const char*)displayTxt);

  int x1 = x + ((SMALL_ICONS_WIDTH - w - w1 - kmoffset) / 2) - kmoffset;
  int y1 = y + 59;
  
  tft.fillRect(x + offset, 
              y1 - 14, SMALL_ICONS_WIDTH - (offset * 2), 
              16, 
              ICONS_BG_COLOR);
  tft.setCursor(x1, y1);
  tft.println(displayTxt);

  switch(mode) {
    default:
    case MODE_M_NORMAL:
      break;
    case MODE_M_TEMP:
      tft.drawCircle(x1 + w + 6, y1 - 10, 3, color);
      break;
    case MODE_M_KILOMETERS:
      tft.setDisplayDefaultFont();
      tft.setCursor(x1 + w + kmoffset, y1 - 6);
      tft.println(km);
      return w;
  }

  tft.setDisplayDefaultFont();
  return w;
}

int SimpleGauge::getBaseX(void) {
  switch(mode) {
    case SIMPLE_G_GPS:
      return (SMALL_ICONS_WIDTH * 0);
    case SIMPLE_G_RPM:
      return (SMALL_ICONS_WIDTH * 1);
    case SIMPLE_G_EGT:
      return (SMALL_ICONS_WIDTH * 2);
    case SIMPLE_G_INTAKE:
      return (SMALL_ICONS_WIDTH * 3);
    case SIMPLE_G_ENGINE_LOAD:
      return (SMALL_ICONS_WIDTH * 4);
  }
  return -1;
}

int SimpleGauge::getBaseY(void) {
  switch(mode) {
    case SIMPLE_G_GPS:
    case SIMPLE_G_RPM:
    case SIMPLE_G_EGT:
    case SIMPLE_G_INTAKE:
    case SIMPLE_G_ENGINE_LOAD:
      return BIG_ICONS_HEIGHT + (BIG_ICONS_OFFSET * 2);
  }
  return -1;
}

void SimpleGauge::redraw(void) {
  drawOnce = true;
}

void SimpleGauge::switchCurrentEGTMode(void) {
  currentIsDPF = !currentIsDPF;
  redraw();
}

void SimpleGauge::resetCurrentEGTMode(void) {
  currentIsDPF = false;
}

void SimpleGauge::showSimpleGauge(void) {

  TFT tft = returnTFTReference();
  int txtMode = 0;
  int color = TEXT_COLOR;
  char *format = NULL;
  int currentVal = 0;
  bool draw = false;

  if(drawOnce) {
    unsigned short *tempImg = NULL;

    switch(mode) {
      case SIMPLE_G_ENGINE_LOAD:
        tempImg = (unsigned short*)pump;
        break;
      case SIMPLE_G_INTAKE:
        tempImg = (unsigned short*)ic;
        break;
      case SIMPLE_G_RPM:
        tempImg = (unsigned short*)rpm;
        break;
      case SIMPLE_G_GPS:
        tempImg = (unsigned short*)gpsIcon;
        break;
      case SIMPLE_G_EGT:
        tempImg = (unsigned short*)egt;
        if(currentIsDPF) {
          tempImg = (unsigned short*)dpf;
        }
        break;
    }
    drawImage(getBaseX(), getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, ICONS_BG_COLOR, tempImg);
    drawOnce = false;
    draw = true;
  } else {

    switch(mode) {
      case SIMPLE_G_ENGINE_LOAD:
        currentVal = (int)valueFields[F_THROTTLE_POS];
        currentVal = getThrottlePercentage(currentVal);
        break;
      case SIMPLE_G_INTAKE:
        currentVal = (int)valueFields[F_INTAKE_TEMP];
        break;
      case SIMPLE_G_RPM:
        currentVal = (int)valueFields[F_RPM];
        break;
      case SIMPLE_G_GPS:
        currentVal = (int)getCurrentCarSpeed();
        break;
    }

    if(lastShowedVal != currentVal) {
      switch(mode) {
        case SIMPLE_G_ENGINE_LOAD:
          drawTextForMiddleIcons(getBaseX(), getBaseY(), 5, 
                                 TEXT_COLOR, MODE_M_NORMAL, (const char*)F("%d%%"), currentVal);
          break;

        case SIMPLE_G_RPM:
          drawTextForMiddleIcons(getBaseX(), getBaseY(), 5, 
                                 TEXT_COLOR, MODE_M_NORMAL, (const char*)F("%d"), currentVal);
          break;

        case SIMPLE_G_GPS:
          drawTextForMiddleIcons(getBaseX(), getBaseY(), 1, 
                                 TEXT_COLOR, MODE_M_KILOMETERS, (const char*)F("%d"), currentVal);
          break;
        
        case SIMPLE_G_INTAKE:

          bool error = currentVal < TEMP_LOWEST || currentVal > TEMP_HIGHEST;

          if(error) {
            color = COLOR(RED);
            format = (char*)err;
          } else {
            format = ((char*)F("%d"));
          }

          txtMode = (error) ? MODE_M_NORMAL : MODE_M_TEMP;
          drawTextForMiddleIcons(getBaseX(), getBaseY(), 5, 
                                 color, txtMode, format, currentVal);
          break;
      }

      switch(mode) {
        case SIMPLE_G_ENGINE_LOAD:
        case SIMPLE_G_RPM:
        case SIMPLE_G_GPS:
        case SIMPLE_G_INTAKE:
          lastShowedVal = currentVal;
          break;
      }
    }

    switch(mode) {
      case SIMPLE_G_GPS:
        int x, y;

        if(isGPSAvailable()) {
          color = COLOR(GREEN);
        } else {
          color = (alertSwitch()) ? COLOR(RED) : ICONS_BG_COLOR;
        }

        int posOffset = 10;
        int radius = 4;

        x = getBaseX() + SMALL_ICONS_WIDTH - posOffset - radius;
        y = getBaseY() + posOffset - 1;

        tft.fillCircle(x, y, radius, color);

        break;
    }
  }

  switch(mode) {
    case SIMPLE_G_EGT:

      currentVal = (int)valueFields[F_EGT];
      if(currentIsDPF) {
        currentVal = (int)valueFields[F_DPF_TEMP];
      }

      if(currentVal < TEMP_EGT_MIN) {
        currentVal = TEMP_EGT_MIN - 1;
      }

      if(lastShowedVal != currentVal) {
        lastShowedVal = currentVal;
        draw = true;
      }

      bool overheat = false;
      if(currentVal > TEMP_EGT_OK_HI) {
        overheat = true;
      }

      if(overheat) {
        draw = true;
        color = (seriousAlertSwitch()) ? COLOR(RED) : TEXT_COLOR;
      }

      if(draw) {

        if(currentVal < TEMP_EGT_MIN) {
          format = ((char*)F("COLD"));
        } else if(currentVal > TEMP_EGT_MAX) {
          format = ((char*)err);  
        } else {
          format = (char*)F("%d");
          if(currentIsDPF) {
            if(isDPFRegenerating()) {
              format = (char*)F("R/%d");          
            }
          } 
        }

        bool isTemp = (currentVal > TEMP_EGT_MIN && currentVal < TEMP_EGT_MAX);
        txtMode = MODE_M_NORMAL;
        if(isTemp) {
          txtMode = MODE_M_TEMP;
        }

        drawTextForMiddleIcons(getBaseX(), getBaseY(), 2, 
                               color, txtMode, format, currentVal);
      }
      break;
  }
}

