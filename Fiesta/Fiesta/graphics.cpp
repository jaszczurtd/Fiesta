
#include "graphics.h"

TFT *tft;
void initTFT(void) {
  tft = new TFTExtension(TFT_CS, TFT_DC, TFT_RST);
  tft->begin();
  tft->setRotation(1);
}

TFT *returnTFTReference(void) {
  if(tft == NULL) {
    initTFT();
  }
  return tft;
}

bool softInitDisplay(void *arg) {
  TFT *tft = returnTFTReference();
  tft->softInit(75);
  tft->setRotation(1);

  return true;
}

void showLogo(void) {
  #ifndef DEBUG_SCREEN
  
  TFT *tft = returnTFTReference();
  tft->fillScreen(COLOR(WHITE));

  int x = (SCREEN_W - FIESTA_LOGO_WIDTH) / 2;
  int y = (SCREEN_H - FIESTA_LOGO_HEIGHT) / 2;
  tft->drawImage(x, y, FIESTA_LOGO_WIDTH, FIESTA_LOGO_HEIGHT, 0xffff, (unsigned short*)FiestaLogo);

  #endif
}

void redrawAll(void) {
  initHeatedWindow();
  initFuelMeasurement();
  redrawFuel();
  redrawTempGauges();
  redrawOilPressure();
  redrawPressure();
  redrawSimpleGauges();
  redrawVolts();
}

//-------------------------------------------------------------------------------------------------
//volt indicator
//-------------------------------------------------------------------------------------------------

static bool v_drawOnce = true; 
static bool drawVolts = false;
void redrawVolts(void) {
    v_drawOnce = true;
}

int v_getBaseX(void) {
    return 223;
}

int v_getBaseY(void) {
    return 185;
}

static int lastV1 = -1, lastV2 = -1;
static const unsigned short *lastImg = NULL;

void showVolts(float volts) {

  TFT *tft = returnTFTReference();
  const unsigned short *img = NULL;
  int v1, v2;

  floatToDec(volts, &v1, &v2);
  if(v1 != lastV1 || v2 != lastV2) {
    lastV1 = v1;
    lastV2 = v2;

    if(volts < VOLTS_MIN_VAL || volts > VOLTS_MAX_VAL) {
      img = batteryNotOKIcon;
    } else {
      img = batteryOKIcon;
    }

    if(img != lastImg) {
      lastImg = img;
      v_drawOnce = true;
    }

    drawVolts = true;
  }

  if(v_drawOnce) {
    tft->drawImage(v_getBaseX(), v_getBaseY(), BATTERY_WIDTH, BATTERY_HEIGHT, ICONS_BG_COLOR, (unsigned short*)img);
    v_drawOnce = false;
  }

  if(drawVolts) {
    int x = v_getBaseX() + BATTERY_WIDTH + 2;
    int y = v_getBaseY() + 26;

    tft->fillRect(x, y - 14, 45, 16, ICONS_BG_COLOR);

    int color = TEXT_COLOR;
    if(volts < VOLTS_MIN_VAL || volts > VOLTS_MAX_VAL) {
        color = COLOR(RED);
    }

    tft->prepareText((const char*)F("%d.%dv"), v1, v2);
    tft->sansBoldWithPosAndColor(x, y, color);
    tft->printlnFromPreparedText();

    drawVolts = false;
  }
}

