
#include "graphics.h"

TFT tft = TFT(TFT_CS, TFT_DC, TFT_RST);
TFT returnReference(void) {
  return tft;
}

void initGraphics(void) {
  // Use this initializer if using a 1.8" TFT screen:
  //tft.initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
  initDisplay();
  tft.setRotation(1);
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

static char displayTxt[8];
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

    tft.fillRect(x, y - 14, 35, 16, BIG_ICONS_BG_COLOR);

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
       -(TEMP_BAR_MAXHEIGHT - currentHeight), BIG_ICONS_BG_COLOR);
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
    return 0; 
}

static int lastCoolantHeight = C_INIT_VAL;
static int lastCoolantVal = C_INIT_VAL;

void showTemperatureAmount(int currentVal, int maxVal) {

    if(t_drawOnce) {
        drawImage(t_getBaseX(), t_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, BIG_ICONS_BG_COLOR, (unsigned short*)temperature);
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

        if(draw) {
            x = t_getBaseX() + 24;
            y = t_getBaseY() + 8 + TEMP_BAR_MAXHEIGHT;

            drawTempBar(x, y, currentHeight, color);

            x = t_getBaseX() + TEMP_DOT_X;
            y = t_getBaseY() + TEMP_DOT_Y;

            tft.fillCircle(x, y, TEMP_BAR_DOT_RADIUS, color);
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
//oil temperature indicator
//-------------------------------------------------------------------------------------------------

static bool o_drawOnce = true; 
void redrawOil(void) {
    o_drawOnce = true;
}

const int o_getBaseX(void) {
    return BIG_ICONS_WIDTH;
}

const int o_getBaseY(void) {
    return 0; 
}

static int lastOilHeight = C_INIT_VAL;
static int lastOilVal = C_INIT_VAL;

void showOilAmount(int currentVal, int maxVal) {

    if(o_drawOnce) {
        drawImage(o_getBaseX(), o_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, 0xf7be, (unsigned short*)oil);
        o_drawOnce = false;
    } else {
        int x, y, color;

        int valToDisplay = currentVal;
        if(currentVal > TEMP_OIL_MAX) {
            currentVal = TEMP_OIL_MAX;
        }

        bool overheat = false;
        color = TEMP_INITIAL_COLOR;
        if(currentVal >= TEMP_OK_LO && currentVal <= TEMP_OIL_OK_HI) {
            color = COLOR(ORANGE);
        } 
        if(currentVal > TEMP_OIL_OK_HI) {
            overheat = true;
        }

        bool blink = alertSwitch();
        if(currentVal > TEMP_OIL_OK_HI + ((TEMP_OIL_MAX - TEMP_OIL_OK_HI) / 2)) {
          blink = seriousAlertSwitch();
        }

        currentVal -= TEMP_MIN;
        maxVal -= TEMP_MIN;
        if(currentVal < 0) {
            currentVal = 0;
        }

        int currentHeight = currentValToHeight(
            (currentVal < TEMP_OIL_MAX) ? currentVal : TEMP_OIL_MAX,
            maxVal);

        bool draw = false;
        if(lastOilHeight != currentHeight) {
            lastOilHeight = currentHeight;
            draw = true;
        }

        if(overheat) {
            draw = true;
            color = (blink) ? COLOR(RED) : COLOR(ORANGE);
        }

        if(draw) {
            x = o_getBaseX() + 20;
            y = o_getBaseY() + 8 + TEMP_BAR_MAXHEIGHT;

            drawTempBar(x, y, currentHeight, color);

            x = o_getBaseX() + OIL_DOT_X;
            y = o_getBaseY() + OIL_DOT_Y;

            tft.fillCircle(x, y, TEMP_BAR_DOT_RADIUS, color);
        }

        if(lastOilVal != valToDisplay) {
            lastOilVal = valToDisplay;

            x = o_getBaseX() + 36;
            y = o_getBaseY() + 38;
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
    return 0;
}

const int e_getBaseY(void) {
    return BIG_ICONS_HEIGHT; 
}

static int lastLoadAmount = C_INIT_VAL;

void showEngineLoadAmount(int currentVal) {

    int value = getThrottlePercentage(currentVal);

    if(e_drawOnce) {
        drawImage(e_getBaseX(), e_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, SMALL_ICONS_BG_COLOR, (unsigned short*)pump);
        e_drawOnce = false;
    } else {
        if(lastLoadAmount != value) {
            lastLoadAmount = value;

            int x, y, w, offset;

            setDisplayDefaultFont();
            tft.setTextColor(TEXT_COLOR);

            w = prepareText((const char*)F("%d%%"), value);

            x = e_getBaseX() + ((SMALL_ICONS_WIDTH - w) / 2);
            y = e_getBaseY() + 30;
            
            offset = 5;
            tft.fillRect(e_getBaseX() + offset, y, SMALL_ICONS_WIDTH - (offset * 2), 8, SMALL_ICONS_BG_COLOR);
            tft.setCursor(x, y);
            tft.println(getPreparedText());
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
    return BIG_ICONS_HEIGHT; 
}

static bool currentIsDPF = false;
bool changeEGT(void *argument) {
  if(isDPFConnected()) {
    currentIsDPF = !currentIsDPF;
  } else {
    currentIsDPF = false;
  }
  egt_drawOnce = true;

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
    drawImage(egt_getBaseX(), egt_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, SMALL_ICONS_BG_COLOR, img);
    egt_drawOnce = false;
    draw = true;
  } 
  int x, y, w, offset, color = TEXT_COLOR;

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
    setDisplayDefaultFont();
    tft.setTextColor(color);

    if(currentVal < TEMP_EGT_MIN) {
      w = prepareText((const char*)F("COLD"));
    } else if(currentVal > TEMP_EGT_MAX) {
      w = prepareText((const char*)err);  
    } else {
      const char *format = (const char*)F("%d");
      if(currentIsDPF) {
        if(isDPFRegenerating()) {
          format = (const char*)F("R/%d");          
        }
      } 
      w = prepareText(format, currentVal);
    }

    x = egt_getBaseX() + (((SMALL_ICONS_WIDTH - w) / 2) - 2);
    y = egt_getBaseY() + 30;
    
    offset = 2;
    tft.fillRect(egt_getBaseX() + offset, y - 2, SMALL_ICONS_WIDTH - (offset * 2), 10, SMALL_ICONS_BG_COLOR);
    tft.setCursor(x, y);
    tft.println(getPreparedText());

    if(currentVal > TEMP_EGT_MIN &&
      currentVal < TEMP_EGT_MAX) {
        tft.drawCircle(x + w + 2, y, 2, color);
    }
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
    return BIG_ICONS_HEIGHT; 
}

static int lastICTempVal = C_INIT_VAL;

void showICTemperatureAmount(int currentVal) {

    if(ic_drawOnce) {
        drawImage(ic_getBaseX(), ic_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, SMALL_ICONS_BG_COLOR, (unsigned short*)ic);
        ic_drawOnce = false;
    } else {
        if(lastICTempVal != currentVal) {
            lastICTempVal = currentVal;

            int x, y, w, offset;

            setDisplayDefaultFont();

            bool error = currentVal < TEMP_LOWEST || currentVal > TEMP_HIGHEST;
            if(error) {
                tft.setTextColor(COLOR(RED));
                w = prepareText((const char*)F("%s"), err);
            } else {
                tft.setTextColor(TEXT_COLOR);
                w = prepareText((const char*)F("%d"), currentVal);
            }

            x = ic_getBaseX() + (((SMALL_ICONS_WIDTH - w) / 2) - 2);
            y = ic_getBaseY() + 30;
            
            offset = 5;
            tft.fillRect(ic_getBaseX() + offset, y - 2, SMALL_ICONS_WIDTH - (offset * 2), 10, SMALL_ICONS_BG_COLOR);
            tft.setCursor(x, y);
            tft.println(getPreparedText());
            
            if(!error) {
                tft.drawCircle(x + w + 2, y, 2, TEXT_COLOR);
            }
        }
    }
}

//-------------------------------------------------------------------------------------------------
//volt indicator
//-------------------------------------------------------------------------------------------------

int v_getBaseX(void) {
    return 0;
}

int v_getBaseY(void) {
    return SCREEN_H - textHeight(err) - (OFFSET / 2);
}

static int lastV1 = -1, lastV2 = -1;

void showVolts(float volts) {
    int v1, v2;

    floatToDec(volts, &v1, &v2);
    if(v1 != lastV1 || v2 != lastV2) {
        lastV1 = v1;
        lastV2 = v2;

        int x = v_getBaseX();
        int y = v_getBaseY();

        int color = VOLTS_OK_COLOR;
        if(volts < 11.5) {
            color = VOLTS_LOW_ERROR_COLOR;
        }
        if(volts > 14.7) {
            color = COLOR(RED);
        }

        tft.setTextColor(color);
        tft.setCursor(x, y);

        prepareText((const char*)F("%d.%dv"), v1, v2);

        tft.fillRect(x, y, 30, 8, COLOR(BLACK));

        tft.setTextSize(1);
        tft.println(getPreparedText());
    }
}

