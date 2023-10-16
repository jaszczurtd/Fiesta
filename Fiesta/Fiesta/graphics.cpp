
#include "graphics.h"

static bool gfxInitialized = false;

TFT tft = TFT(TFT_CS, TFT_DC, TFT_RST);
TFT returnTFTReference(void) {
  return tft;
}

bool softInitDisplay(void *arg) {
  if(gfxInitialized) {
    tft.softInit(75);
    tft.setRotation(1);
  }

  return true;
}

void initGraphics(void) {
  initDisplay();
  tft.setRotation(1);
  gfxInitialized = true;
}

void fillScreenWithColor(int c) {
  tft.fillScreen(c);
}

void showLogo(void) {
  #ifndef DEBUG_SCREEN

  fillScreenWithColor(COLOR(WHITE));

  int x = (SCREEN_W - FIESTA_LOGO_WIDTH) / 2;
  int y = (SCREEN_H - FIESTA_LOGO_HEIGHT) / 2;
  tft.drawImage(x, y, FIESTA_LOGO_WIDTH, FIESTA_LOGO_HEIGHT, 0xffff, (unsigned short*)FiestaLogo);

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

int currentValToHeight(int currentVal, int maxVal) {
  float percent = (float)(currentVal * 100) / (float)maxVal;
  return percentToGivenVal(percent, TEMP_BAR_MAXHEIGHT);
}

void drawImage(int x, int y, int width, int height, int background, unsigned short *pointer) {
  tft.drawImage(x, y, width, height, background, pointer);
}

int textWidth(const char* text) {
  return tft.textWidth(text);
}

int textHeight(const char* text) {
  return tft.textHeight(text);
}

void setDisplayDefaultFont(void) {
  tft.setDisplayDefaultFont();
}

static char displayTxt[32];
const char *getPreparedText(void) {
  return displayTxt;
}

int prepareText(const char *format, ...) {

    va_list valist;
    va_start(valist, format);

    memset(displayTxt, 0, sizeof(displayTxt));
    vsnprintf(displayTxt, sizeof(displayTxt) - 1, format, valist);
    va_end(valist);

    return tft.textWidth((const char*)displayTxt);
}

void drawTextForPressureIndicators(int x, int y, const char *format, ...) {

  memset(displayTxt, 0, sizeof(displayTxt));

  va_list valist;
  va_start(valist, format);
  vsnprintf(displayTxt, sizeof(displayTxt) - 1, format, valist);
  va_end(valist);

  int x1 = x + BAR_TEXT_X;
  int y1 = y + BAR_TEXT_Y - 12;

  tft.fillRect(x1, y1, 28, 15, ICONS_BG_COLOR);

  x1 = x + BAR_TEXT_X;
  y1 = y + BAR_TEXT_Y;

  tft.sansBoldWithPosAndColor(x1, y1, TEXT_COLOR);
  tft.println(getPreparedText());

  x1 = x + BAR_TEXT_X + 25;
  y1 = y + BAR_TEXT_Y - 6;
  tft.defaultFontWithPosAndColor(x1, y1, TEXT_COLOR);
  tft.println(F("BAR"));
}

void displayErrorWithMessage(int x, int y, const char *msg) {
    int workingx = x; 
    int workingy = y;

    tft.fillCircle(workingx, workingy, 10, COLOR(RED));
    workingx += 20;
    tft.fillCircle(workingx, workingy, 10, COLOR(RED));

    workingy += 5;
    workingx = x + 4;

    tft.fillRoundRect(workingx, workingy, 15, 40, 6, COLOR(RED));
    workingy +=30;
    tft.drawLine(workingx, workingy, workingx + 14, workingy, COLOR(BLACK));

    workingx +=6;
    workingy +=4;
    tft.drawLine(workingx, workingy, workingx, workingy + 5, COLOR(BLACK));
    tft.drawLine(workingx + 1, workingy, workingx + 1, workingy + 5, COLOR(BLACK));

    workingx = x -16;
    workingy = y;
    tft.drawLine(workingx, workingy, workingx + 15, workingy, COLOR(BLACK));

    workingy = y - 8;
    tft.drawLine(workingx, workingy, workingx + 15, y - 2, COLOR(BLACK));

    workingy = y + 8;
    tft.drawLine(workingx, workingy, workingx + 15, y + 2, COLOR(BLACK));

    workingx = x + 23;
    workingy = y;
    tft.drawLine(workingx, workingy, workingx + 15, workingy, COLOR(BLACK));
  
    workingy = y - 3;
    tft.drawLine(workingx, workingy + 1, workingx + 15, y - 8, COLOR(BLACK));

    workingy = y + 1;
    tft.drawLine(workingx, workingy + 1, workingx + 15, y + 8, COLOR(BLACK));

    tft.defaultFontWithPosAndColor(x + 8, y + 46, COLOR(BLUE));
    tft.println(msg);
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
    drawImage(v_getBaseX(), v_getBaseY(), BATTERY_WIDTH, BATTERY_HEIGHT, ICONS_BG_COLOR, (unsigned short*)img);
    v_drawOnce = false;
  }

  if(drawVolts) {
    int x = v_getBaseX() + BATTERY_WIDTH + 2;
    int y = v_getBaseY() + 26;

    tft.fillRect(x, y - 14, 45, 16, ICONS_BG_COLOR);

    int color = TEXT_COLOR;
    if(volts < VOLTS_MIN_VAL || volts > VOLTS_MAX_VAL) {
        color = COLOR(RED);
    }

    prepareText((const char*)F("%d.%dv"), v1, v2);
    tft.sansBoldWithPosAndColor(x, y, color);
    tft.println(getPreparedText());

    drawVolts = false;
  }
}

