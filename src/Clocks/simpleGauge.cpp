
#include "simpleGauge.h"

SimpleGauge engineLoad_g = SimpleGauge(SIMPLE_G_ENGINE_LOAD);
SimpleGauge intake_g = SimpleGauge(SIMPLE_G_INTAKE);
SimpleGauge rpm_g = SimpleGauge(SIMPLE_G_RPM);
SimpleGauge gps_g = SimpleGauge(SIMPLE_G_GPS);
SimpleGauge egt_g = SimpleGauge(SIMPLE_G_EGT);
SimpleGauge volts_g = SimpleGauge(SIMPLE_G_VOLTS);
SimpleGauge ecu_g = SimpleGauge(SIMPLE_G_ECU);
SimpleGauge oil_speed_g = SimpleGauge(SIMPLE_G_SPEED_AND_OIL);

void redrawSimpleGauges(void) {
  engineLoad_g.redraw();
  intake_g.redraw();
  rpm_g.redraw();
  gps_g.redraw();
  egt_g.redraw();
  volts_g.redraw();
  ecu_g.redraw();
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
  volts_g.showSimpleGauge();
}

void showECUConnectionGauge(void) {
  ecu_g.showSimpleGauge();
  oil_speed_g.showSimpleGauge();
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
  resetCurrentEGTMode();
  lastV1 = lastV2 = C_INIT_VAL;

  switch(mode) {
    case SIMPLE_G_ECU:
      lastShowedVal = isEcuConnected();
    break;

    case SIMPLE_G_SPEED_AND_OIL:
      lastShowedVal = isOilSpeedModuleConnected();
    break;

    default:
      lastShowedVal = C_INIT_VAL;
    break;
  }
}

int SimpleGauge::drawTextForMiddleIcons(int x, int y, int offset, int color, int mode, const char *format, ...) {

  TFT *tft = returnTFTReference();

  int w1 = 0, kmoffset = 0;
  const char *km = ((const char*)F("km/h"));
  if(mode == MODE_M_KILOMETERS) {
    tft->setDisplayDefaultFont();
    w1 = tft->textWidth(km);
    kmoffset = 5;
  }
  tft->serif9ptWithColor(color);

  memset(displayTxt, 0, sizeof(displayTxt));

  va_list valist;
  va_start(valist, format);
  vsnprintf(displayTxt, sizeof(displayTxt) - 1, format, valist);
  va_end(valist);

  int w = tft->textWidth((const char*)displayTxt);

  int x1 = x + ((SMALL_ICONS_WIDTH - w - w1 - kmoffset) / 2) - kmoffset;
  int y1 = y + 59;
  
  tft->fillRect(x + offset, 
              y1 - 14, SMALL_ICONS_WIDTH - (offset * 2), 
              16, 
              ICONS_BG_COLOR);
  tft->setCursor(x1, y1);
  tft->println(displayTxt);

  switch(mode) {
    default:
    case MODE_M_NORMAL:
      break;
    case MODE_M_TEMP:
      tft->drawCircle(x1 + w + 6, y1 - 10, 3, color);
      break;
    case MODE_M_KILOMETERS:
      tft->setDisplayDefaultFont();
      tft->setCursor(x1 + w + kmoffset, y1 - 6);
      tft->println(km);
      return w;
  }

  tft->setDisplayDefaultFont();
  return w;
}

int SimpleGauge::getBaseX(void) {
  switch(mode) {
    case SIMPLE_G_GPS:
      return (SMALL_ICONS_WIDTH * 0) + 8;
    case SIMPLE_G_RPM:
      return (SMALL_ICONS_WIDTH * 1) + 1;
    case SIMPLE_G_EGT:
      return (SMALL_ICONS_WIDTH * 2);
    case SIMPLE_G_INTAKE:
      return (SMALL_ICONS_WIDTH * 3);
    case SIMPLE_G_ENGINE_LOAD:
      return (SMALL_ICONS_WIDTH * 4);
    case SIMPLE_G_VOLTS:
      return 223;
    case SIMPLE_G_ECU:
      return SCREEN_W - (3 * ECU_CONNECTION_RADIUS);
    case SIMPLE_G_SPEED_AND_OIL:
      return SCREEN_W - (6 * ECU_CONNECTION_RADIUS);
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
    case SIMPLE_G_VOLTS:
      return 185;
    case SIMPLE_G_SPEED_AND_OIL:
    case SIMPLE_G_ECU:
      return SCREEN_H - (3 * ECU_CONNECTION_RADIUS);
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

  TFT *tft = returnTFTReference();
  unsigned short *tempImg = NULL;
  bool draw = false;
  int x, y;

  float volts = valueFields[F_VOLTS];
  int v1, v2;
  int color = TEXT_COLOR;

  switch(mode) {

    case SIMPLE_G_VOLTS:
      floatToDec(volts, &v1, &v2);
      if(v1 != lastV1 || v2 != lastV2) {
        lastV1 = v1;
        lastV2 = v2;

        if(volts < VOLTS_MIN_VAL || volts > VOLTS_MAX_VAL) {
          tempImg = (unsigned short *)batteryNotOKIcon;
        } else {
          tempImg = (unsigned short *)batteryOKIcon;
        }

        drawOnce = true;
        draw = true;
      }
      break;

    case SIMPLE_G_ECU:
      if(!isEcuConnected()) {
        draw = true;
      }
      break;

    case SIMPLE_G_SPEED_AND_OIL:
      if(!isOilSpeedModuleConnected()) {
        draw = true;
      }
      break;
  }

  int w = SMALL_ICONS_WIDTH;
  int h = SMALL_ICONS_HEIGHT;
  if(drawOnce) {
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
      case SIMPLE_G_VOLTS:
        w = BATTERY_WIDTH;
        h = BATTERY_HEIGHT;
        break;
    }

    if(tempImg != NULL) {
      tft->drawImage(getBaseX(), getBaseY(), w, h, ICONS_BG_COLOR, tempImg);
    }
    drawOnce = false;
    draw = true;
  }

  int currentVal = 0;

  switch(mode) {
    case SIMPLE_G_ENGINE_LOAD:
      currentVal = getThrottlePercentage();
      break;
    case SIMPLE_G_INTAKE:
      currentVal = int(valueFields[F_INTAKE_TEMP]);
      break;
    case SIMPLE_G_RPM:
      currentVal = getEngineRPM();
      break;
    case SIMPLE_G_GPS:
      currentVal = getCurrentCarSpeed();
      break;
    case SIMPLE_G_ECU:
      currentVal = isEcuConnected();
      break;
    case SIMPLE_G_SPEED_AND_OIL:
      currentVal = isOilSpeedModuleConnected();
      break;
  }

  const char *format = NULL;
  int txtMode = MODE_M_NORMAL;
  int offset = 5;

  if(lastShowedVal != currentVal) {
    switch(mode) {
      case SIMPLE_G_ENGINE_LOAD:
        format = (const char*)F("%d%%");
        break;

      case SIMPLE_G_RPM:
        format = (const char*)F("%d");
        break;

      case SIMPLE_G_GPS:
        offset = 1;
        format = (const char*)F("%d");
        txtMode = MODE_M_KILOMETERS;
        break;
      
      case SIMPLE_G_INTAKE:
        bool error = currentVal < TEMP_LOWEST || currentVal > TEMP_HIGHEST;

        if(error) {
          color = COLOR(RED);
          format = (const char*)err;
        } else {
          format = ((const char*)F("%d"));
        }

        txtMode = (error) ? MODE_M_NORMAL : MODE_M_TEMP;
        break;
    }

    switch(mode) {
      case SIMPLE_G_ENGINE_LOAD:
      case SIMPLE_G_RPM:
      case SIMPLE_G_GPS:
      case SIMPLE_G_INTAKE:
        drawTextForMiddleIcons(getBaseX(), getBaseY(), offset, 
                                color, txtMode, format, currentVal);
        lastShowedVal = currentVal;
        break;

      case SIMPLE_G_SPEED_AND_OIL:
      case SIMPLE_G_ECU:
        draw = true;
        drawOnce = true;
        lastShowedVal = currentVal;
        break;
    }
  }

  switch(mode) {
    case SIMPLE_G_GPS: {

      if(isGPSAvailable()) {
        color = COLOR(GREEN);
      } else {
        color = (alertSwitch()) ? COLOR(RED) : ICONS_BG_COLOR;
      }

      int posOffset = 10;
      int radius = 4;

      x = getBaseX() + SMALL_ICONS_WIDTH - posOffset - radius;
      y = getBaseY() + posOffset - 1;

      tft->fillCircle(x, y, radius, color);

    }
    break;

    case SIMPLE_G_EGT: {
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
    }
    break;

    case SIMPLE_G_VOLTS: {
      if(draw) {
        x = getBaseX() + BATTERY_WIDTH + 2;
        y = getBaseY() + 26;

        tft->fillRect(x, y - 14, 45, 16, ICONS_BG_COLOR);

        color = TEXT_COLOR;
        if(volts < VOLTS_MIN_VAL || volts > VOLTS_MAX_VAL) {
            color = COLOR(RED);
        }

        char txt[DISPLAY_TXT_SIZE];
        tft->prepareText(txt, (const char*)F("%d.%dv"), v1, v2);
        tft->sansBoldWithPosAndColor(x, y, color);
        tft->printlnFromPreparedText(txt);
      }
    }
    break;

    case SIMPLE_G_SPEED_AND_OIL:
    case SIMPLE_G_ECU: {
      x = getBaseX();
      y = getBaseY();

      if(draw) {    

        switch(mode) {
          case SIMPLE_G_ECU:
          default:
            color = (seriousAlertSwitch()) ? COLOR(RED) : ICONS_BG_COLOR;
          break;

          case SIMPLE_G_SPEED_AND_OIL:
            color = (!seriousAlertSwitch()) ? COLOR(PURPLE) : ICONS_BG_COLOR;
          break;
        }

        tft->fillCircle(x, y, ECU_CONNECTION_RADIUS, color);
        draw = false;
      }

      if(drawOnce) {
        tft->fillRect(x - ECU_CONNECTION_RADIUS, y - ECU_CONNECTION_RADIUS, 
                      ECU_CONNECTION_RADIUS * 3, ECU_CONNECTION_RADIUS * 3, ICONS_BG_COLOR);
        drawOnce = false;
      }
    }
    break;

  }
}

