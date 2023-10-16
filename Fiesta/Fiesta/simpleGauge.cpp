
#include "simpleGauge.h"

SimpleGauge engineLoad_g = SimpleGauge(SIMPLE_G_ENGINE_LOAD);
SimpleGauge intake_g = SimpleGauge(SIMPLE_G_INTAKE);
SimpleGauge rpm_g = SimpleGauge(SIMPLE_G_RPM);
SimpleGauge gps_g = SimpleGauge(SIMPLE_G_GPS);

void redrawSimpleGauges(void) {
  engineLoad_g.redraw();
  intake_g.redraw();
  rpm_g.redraw();
  gps_g.redraw();
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

SimpleGauge::SimpleGauge(int mode) {
  this->mode = mode;
  drawOnce = true;
  lastShowedVal = C_INIT_VAL;
}

int SimpleGauge::getBaseX(void) {
  switch(mode) {
    case SIMPLE_G_GPS:
      return 0;
    case SIMPLE_G_RPM:
      return SMALL_ICONS_WIDTH;
    case SIMPLE_G_ENGINE_LOAD:
      return (SMALL_ICONS_WIDTH * 4);
    case SIMPLE_G_INTAKE:
      return (SMALL_ICONS_WIDTH * 3);
  }
  return -1;
}

int SimpleGauge::getBaseY(void) {
  switch(mode) {
    case SIMPLE_G_GPS:
    case SIMPLE_G_ENGINE_LOAD:
    case SIMPLE_G_INTAKE:
    case SIMPLE_G_RPM:
      return BIG_ICONS_HEIGHT + (BIG_ICONS_OFFSET * 2);
  }
  return -1;
}

void SimpleGauge::redraw(void) {
  drawOnce = true;
}

void SimpleGauge::showSimpleGauge(void) {

  TFT tft = returnTFTReference();
  unsigned short *tempImg = NULL;

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
    }
    drawImage(getBaseX(), getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, ICONS_BG_COLOR, tempImg);
    drawOnce = false;
  } else {

    int currentVal = 0;
    int color = TEXT_COLOR;

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
      lastShowedVal = currentVal;

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

          char *format = NULL;
          bool error = currentVal < TEMP_LOWEST || currentVal > TEMP_HIGHEST;

          if(error) {
            color = COLOR(RED);
            format = (char*)err;
          } else {
            format = ((char*)F("%d"));
          }

          int mode = (error) ? MODE_M_NORMAL : MODE_M_TEMP;
          drawTextForMiddleIcons(getBaseX(), getBaseY(), 5, 
                                 color, mode, format, currentVal);
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
}

