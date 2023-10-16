
#include "simpleGauge.h"

SimpleGauge engineLoad_g = SimpleGauge(SIMPLE_G_ENGINE_LOAD);

void redrawSimpleGauges(void) {
  engineLoad_g.redraw();
}

void showEngineLoadGauge(void) {
  engineLoad_g.showSimpleGauge();
}

SimpleGauge::SimpleGauge(int mode) {
  this->mode = mode;
  drawOnce = true;
  lastLoadAmount = C_INIT_VAL;
}

int SimpleGauge::getBaseX(void) {
  switch(mode) {
    case SIMPLE_G_ENGINE_LOAD:
      return (SMALL_ICONS_WIDTH * 4);
  }
  return -1;
}

int SimpleGauge::getBaseY(void) {
  switch(mode) {
    case SIMPLE_G_ENGINE_LOAD:
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
    }

    drawImage(getBaseX(), getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, ICONS_BG_COLOR, tempImg);
    drawOnce = false;
  } else {

    int value = 0;
    int currentVal = 0;

    switch(mode) {
      case SIMPLE_G_ENGINE_LOAD:
        currentVal = (int)valueFields[F_THROTTLE_POS];
        value = getThrottlePercentage(currentVal);
        break;
    }

    if(lastLoadAmount != value) {
      lastLoadAmount = value;
      drawTextForMiddleIcons(getBaseX(), getBaseY(), 5, 
                             TEXT_COLOR, MODE_M_NORMAL, (const char*)F("%d%%"), value);
    }
  }
}

