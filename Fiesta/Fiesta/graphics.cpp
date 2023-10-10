
#include "graphics.h"

static bool gfxInitialized = false;

TFT tft = TFT(TFT_CS, TFT_DC, TFT_RST);
TFT returnReference(void) {
  return tft;
}

bool softInitDisplay(void *arg) {
  if(gfxInitialized) {
    tft.softReset(75);
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
  drawImage(x, y, FIESTA_LOGO_WIDTH, FIESTA_LOGO_HEIGHT, 0xffff, (unsigned short*)FiestaLogo);

  #endif
}

void redrawAll(void) {
  initHeatedWindow();
  initFuelMeasurement();
  redrawFuel();
  redrawTemperature();
  redrawOil();
  redrawOilPressure();
  redrawPressure();
  redrawIntercooler();
  redrawEngineLoad();
  redrawRPM();
  redrawEGT();
  redrawVolts();
  redrawGPS();
}

int currentValToHeight(int currentVal, int maxVal) {
    float percent = (float)(currentVal * 100) / (float)maxVal;
    return percentToGivenVal(percent, TEMP_BAR_MAXHEIGHT);
}

void drawImage(int x, int y, int width, int height, int background, unsigned short *pointer) {
    tft.fillRect(x, y, width, height, background);
    tft.drawRGBBitmap(x, y, pointer, width,
                     height);
}

int textWidth(const char* text) {
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return w;
}

int textHeight(const char* text) {
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return h;
}

void setDisplayDefaultFont(void) {
  tft.setFont();
  tft.setTextSize(1);
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

    return textWidth((const char*)displayTxt);
}

void drawTempValue(int x, int y, int valToDisplay) {
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);

  tft.setTextColor(TEXT_COLOR);
  tft.setCursor(x, y);

  tft.fillRect(x, y - 14, 35, 16, ICONS_BG_COLOR);

  if(valToDisplay < TEMP_LOWEST || valToDisplay > TEMP_HIGHEST) {
      tft.setTextColor(COLOR(RED));
      tft.println(err);
      return;
  } else {
      prepareText((const char*)F("%d"), valToDisplay);
      tft.println(getPreparedText());
  }
  setDisplayDefaultFont();
}

void drawTempBar(int x, int y, int currentHeight, int color) {
  tft.fillRect(x, y, TEMP_BAR_WIDTH, -currentHeight, color);
  tft.fillRect(x, y - currentHeight, TEMP_BAR_WIDTH, 
      -(TEMP_BAR_MAXHEIGHT - currentHeight), ICONS_BG_COLOR);
}

int drawTextForMiddleIcons(int x, int y, int offset, int color, int mode, const char *format, ...) {

  int w1 = 0, kmoffset = 0;
  const char *km = ((const char*)F("km/h"));
  if(mode == MODE_M_KILOMETERS) {
    setDisplayDefaultFont();
    w1 = textWidth(km);
    kmoffset = 5;
  }

  tft.setFont(&FreeSerif9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(color);

  memset(displayTxt, 0, sizeof(displayTxt));

  va_list valist;
  va_start(valist, format);
  vsnprintf(displayTxt, sizeof(displayTxt) - 1, format, valist);
  va_end(valist);

  int w = textWidth((const char*)displayTxt);

  int x1 = x + ((SMALL_ICONS_WIDTH - w - w1 - kmoffset) / 2) - kmoffset;
  int y1 = y + 59;
  
  tft.fillRect(x + offset, 
              y1 - 14, SMALL_ICONS_WIDTH - (offset * 2), 
              16, 
              ICONS_BG_COLOR);
  tft.setCursor(x1, y1);
  tft.println(getPreparedText());

  switch(mode) {
    default:
    case MODE_M_NORMAL:
      break;
    case MODE_M_TEMP:
      tft.drawCircle(x1 + w + 6, y1 - 10, 3, color);
      break;
    case MODE_M_KILOMETERS:
      setDisplayDefaultFont();
      tft.setCursor(x1 + w + kmoffset, y1 - 6);
      tft.println(km);
      break;
  }

  setDisplayDefaultFont();
  return w;
}

void drawTextForPressureIndicators(int x, int y, const char *format, ...) {

  memset(displayTxt, 0, sizeof(displayTxt));

  va_list valist;
  va_start(valist, format);
  vsnprintf(displayTxt, sizeof(displayTxt) - 1, format, valist);
  va_end(valist);

  int w = textWidth((const char*)displayTxt);

  int x1 = x + BAR_TEXT_X;
  int y1 = y + BAR_TEXT_Y - 12;

  tft.fillRect(x1, y1, 28, 15, ICONS_BG_COLOR);

  x1 = x + BAR_TEXT_X;
  y1 = y + BAR_TEXT_Y;

  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_COLOR);
  tft.setCursor(x1, y1);
  tft.println(getPreparedText());

  tft.setFont();
  tft.setTextSize(1);

  x1 = x + BAR_TEXT_X + 25;
  y1 = y + BAR_TEXT_Y - 6;
  tft.setCursor(x1, y1);
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

    tft.setCursor(x + 8, y + 46);
    tft.setTextColor(COLOR(BLUE));
    tft.setTextSize(1);
    tft.println(msg);
}

//-------------------------------------------------------------------------------------------------
//coolant temperature indicator
//-------------------------------------------------------------------------------------------------

const char *err = (char*)F("ERR");

static bool t_drawOnce = true; 
void redrawTemperature(void) {
    t_drawOnce = true;
}

const int t_getBaseX(void) {
    return 0;
}

const int t_getBaseY(void) {
    return BIG_ICONS_OFFSET; 
}

static int lastCoolantHeight = C_INIT_VAL;
static int lastCoolantVal = C_INIT_VAL;

static unsigned short *lastFanImg = NULL;
static bool lastFanEnabled = false;

void showTemperatureAmount(int currentVal, int maxVal) {

    if(t_drawOnce) {
        drawImage(t_getBaseX(), t_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, ICONS_BG_COLOR, (unsigned short*)temperature);
        t_drawOnce = false;
    } else {
        int x, y, color;

        int valToDisplay = currentVal;
        if(currentVal > TEMP_MAX) {
            currentVal = TEMP_MAX;
        }

        bool overheat = false;
        color = TEMP_INITIAL_COLOR;
        if(currentVal >= TEMP_OK_LO && currentVal <= TEMP_OK_HI) {
            color = COLOR(ORANGE);
        } 
        if(currentVal > TEMP_OK_HI) {
            overheat = true;
        }

        bool blink = alertSwitch();
        if(currentVal > TEMP_OK_HI + ((TEMP_MAX - TEMP_OK_HI) / 2)) {
          blink = seriousAlertSwitch();
        }

        currentVal -= TEMP_MIN;
        maxVal -= TEMP_MIN;
        if(currentVal < 0) {
            currentVal = 0;
        }

        int currentHeight = currentValToHeight(
            (currentVal < TEMP_MAX) ? currentVal : TEMP_MAX,
            maxVal);

        bool draw = false;
        if(lastCoolantHeight != currentHeight) {
            lastCoolantHeight = currentHeight;
            draw = true;
        }

        if(overheat) {
            draw = true;
            color = (blink) ? COLOR(RED) : COLOR(ORANGE);
        }

        unsigned short *img = NULL;
        bool fanEnabled = isFanEnabled();
        
        if(fanEnabled) {
          if(seriousAlertSwitch()) {
            img = (unsigned short*)fan_b_Icon;
          } else {
            img = (unsigned short*)fan_a_Icon;
          }

          if(img != lastFanImg) {
            lastFanImg - img;
            draw = true;
          }
        }
        if(lastFanEnabled != fanEnabled) {
          lastFanEnabled = fanEnabled;
          draw = true;
        }

        if(draw) {
            x = t_getBaseX() + 24;
            y = t_getBaseY() + 8 + TEMP_BAR_MAXHEIGHT;

            drawTempBar(x, y, currentHeight, color);
            
            if(fanEnabled && img != NULL) {
              x = t_getBaseX() + FAN_COOLANT_X;
              y = t_getBaseY() + FAN_COOLANT_Y;

              drawImage(x, y, FAN_COOLANT_WIDTH, FAN_COOLANT_HEIGHT, ICONS_BG_COLOR, img);
            } else {
              x = t_getBaseX() + TEMP_DOT_X;
              y = t_getBaseY() + TEMP_DOT_Y;

              tft.fillCircle(x, y, TEMP_BAR_DOT_RADIUS, color);
            }
        }

        if(lastCoolantVal != valToDisplay) {
            lastCoolantVal = valToDisplay;

            x = t_getBaseX() + 40;
            y = t_getBaseY() + 38;
            drawTempValue(x, y, valToDisplay);
        }
   }
}

//-------------------------------------------------------------------------------------------------
//engine load indicator
//-------------------------------------------------------------------------------------------------

static bool e_drawOnce = true; 
void redrawEngineLoad(void) {
    e_drawOnce = true;
}

const int e_getBaseX(void) {
    return (SMALL_ICONS_WIDTH * 4);
}

const int e_getBaseY(void) {
    return BIG_ICONS_HEIGHT + (BIG_ICONS_OFFSET * 2); 
}

static int lastLoadAmount = C_INIT_VAL;

void showEngineLoadAmount(int currentVal) {

  int value = getThrottlePercentage(currentVal);

  if(e_drawOnce) {
    drawImage(e_getBaseX(), e_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, ICONS_BG_COLOR, (unsigned short*)pump);
    e_drawOnce = false;
  } else {
    if(lastLoadAmount != value) {
      lastLoadAmount = value;
      drawTextForMiddleIcons(e_getBaseX(), e_getBaseY(), 5, 
                             TEXT_COLOR, MODE_M_NORMAL, (const char*)F("%d%%"), value);
    }
  }
}

//-------------------------------------------------------------------------------------------------
//engine EGT
//-------------------------------------------------------------------------------------------------

static bool egt_drawOnce = true; 
void redrawEGT(void) {
    egt_drawOnce = true;
}

const int egt_getBaseX(void) {
    return (2 * SMALL_ICONS_WIDTH);
}

const int egt_getBaseY(void) {
    return BIG_ICONS_HEIGHT + (BIG_ICONS_OFFSET * 2); 
}

static bool currentIsDPF = false;
bool changeEGT(void *argument) {
  if(isDPFConnected()) {
    currentIsDPF = !currentIsDPF;
    egt_drawOnce = true;
  } else {
    currentIsDPF = false;
  }

  return true;
}

static int lastEGTTempVal = C_INIT_VAL;

void showEGTTemperatureAmount(void) {

  bool draw = false;

  int currentVal = (int)valueFields[F_EGT];
  if(currentIsDPF) {
    currentVal = (int)valueFields[F_DPF_TEMP];
  }  

  if(egt_drawOnce) {
    unsigned short *img = (unsigned short*)egt;
    if(currentIsDPF) {
      img = (unsigned short*)dpf;
    }      
    drawImage(egt_getBaseX(), egt_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, ICONS_BG_COLOR, img);
    egt_drawOnce = false;
    draw = true;
  } 
  int color = TEXT_COLOR;

  if(currentVal < TEMP_EGT_MIN) {
    currentVal = TEMP_EGT_MIN - 1;
  }

  if(lastEGTTempVal != currentVal) {
    lastEGTTempVal = currentVal;
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
    char *format = NULL;

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
    int mode = MODE_M_NORMAL;
    if(isTemp) {
      mode = MODE_M_TEMP;
    }

    drawTextForMiddleIcons(egt_getBaseX(), egt_getBaseY(), 2, 
                          color, mode, format, currentVal);
  }
}

//-------------------------------------------------------------------------------------------------
//intercooler - intake temp
//-------------------------------------------------------------------------------------------------

static bool ic_drawOnce = true; 
void redrawIntercooler(void) {
    ic_drawOnce = true;
}

const int ic_getBaseX(void) {
    return (3 * SMALL_ICONS_WIDTH);
}

const int ic_getBaseY(void) {
    return BIG_ICONS_HEIGHT + (BIG_ICONS_OFFSET * 2); 
}

static int lastICTempVal = C_INIT_VAL;

void showICTemperatureAmount(int currentVal) {

    if(ic_drawOnce) {
        drawImage(ic_getBaseX(), ic_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, ICONS_BG_COLOR, (unsigned short*)ic);
        ic_drawOnce = false;
    } else {
        if(lastICTempVal != currentVal) {
            lastICTempVal = currentVal;

            int color = TEXT_COLOR;
            char *format = NULL;
            bool error = currentVal < TEMP_LOWEST || currentVal > TEMP_HIGHEST;

            if(error) {
                color = COLOR(RED);
                format = (char*)err;
            } else {
                format = ((char*)F("%d"));
            }

            int mode = (error) ? MODE_M_NORMAL : MODE_M_TEMP;
            drawTextForMiddleIcons(ic_getBaseX(), ic_getBaseY(), 5, 
                                   color, mode, format, currentVal);
        }
    }
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

    tft.setCursor(x, y);

    prepareText((const char*)F("%d.%dv"), v1, v2);

    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setCursor(x, y);

    tft.setTextColor(color);
    tft.println(getPreparedText());

    drawVolts = false;
  }
}

