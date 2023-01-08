
#include "graphics.h"

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Adafruit_ST7735 returnReference(void) {
  return tft;
}

void initGraphics(void) {
  // Use this initializer if using a 1.8" TFT screen:
  tft.initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
  tft.setRotation(1);
}

void showLogo(void) {
  #ifndef DEBUG_SCREEN

  tft.fillScreen(ST7735_WHITE);

  int x = (SCREEN_W - FIESTA_LOGO_WIDTH) / 2;
  int y = (SCREEN_H - FIESTA_LOGO_HEIGHT) / 2;
  drawImage(x, y, FIESTA_LOGO_WIDTH, FIESTA_LOGO_HEIGHT, 0xffff, (unsigned short*)FiestaLogo);

  #endif
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

void drawTempValue(int x, int y, int valToDisplay) {
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(x, y);

    tft.fillRect(x, y, 24, 8, BIG_ICONS_BG_COLOR);

    if(valToDisplay < TEMP_LOWEST || valToDisplay > TEMP_HIGHEST) {
        tft.setTextColor(ST77XX_RED);
        tft.println(err);
        return;
    } else {
        char temp[8];
        memset(temp, 0, sizeof(temp));

        snprintf(temp, sizeof(temp) - 1, (const char*)F("%d"), valToDisplay);
        tft.println(temp);
    }
}

void drawTempBar(int x, int y, int currentHeight, int color) {
    tft.fillRect(x, y, 3, currentHeight, color);
    tft.fillRect(x, y - 2, 3, 2, BIG_ICONS_BG_COLOR);
}

void displayErrorWithMessage(int x, int y, const char *msg) {
    int workingx = x; 
    int workingy = y;

    tft.fillCircle(workingx, workingy, 10, ST77XX_RED);
    workingx += 20;
    tft.fillCircle(workingx, workingy, 10, ST77XX_RED);

    workingy += 5;
    workingx = x + 4;

    tft.fillRoundRect(workingx, workingy, 15, 40, 6, ST77XX_RED);
    workingy +=30;
    tft.drawLine(workingx, workingy, workingx + 14, workingy, ST77XX_BLACK);

    workingx +=6;
    workingy +=4;
    tft.drawLine(workingx, workingy, workingx, workingy + 5, ST77XX_BLACK);
    tft.drawLine(workingx + 1, workingy, workingx + 1, workingy + 5, ST77XX_BLACK);

    workingx = x -16;
    workingy = y;
    tft.drawLine(workingx, workingy, workingx + 15, workingy, ST77XX_BLACK);

    workingy = y - 8;
    tft.drawLine(workingx, workingy, workingx + 15, y - 2, ST77XX_BLACK);

    workingy = y + 8;
    tft.drawLine(workingx, workingy, workingx + 15, y + 2, ST77XX_BLACK);

    workingx = x + 23;
    workingy = y;
    tft.drawLine(workingx, workingy, workingx + 15, workingy, ST77XX_BLACK);
  
    workingy = y - 3;
    tft.drawLine(workingx, workingy + 1, workingx + 15, y - 8, ST77XX_BLACK);

    workingy = y + 1;
    tft.drawLine(workingx, workingy + 1, workingx + 15, y + 8, ST77XX_BLACK);

    tft.setCursor(x + 8, y + 46);
    tft.setTextColor(ST77XX_BLUE);
    tft.setTextSize(1);
    tft.println(msg);
}


//-------------------------------------------------------------------------------------------------
//coolant temperature indicator
//-------------------------------------------------------------------------------------------------


const char *half = (char*)F("1/2");
const char *full = (char*)F("F");
const char *empty = (char*)F("E");
const char *emptyMessage = (char*)F("Pusty bak!");
const char *err = (char*)F("ERR");

static char displayTxt[8];

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
        if(currentVal >= TEMP_OK_LO && currentVal <= TEMP_OIL_OK_HI) {
            color = ST77XX_ORANGE;
        } 
        if(currentVal > TEMP_OIL_OK_HI) {
            overheat = true;
        }

        currentVal -= TEMP_MIN;
        maxVal -= TEMP_MIN;
        if(currentVal < 0) {
            currentVal = 0;
        }

        int currentHeight = currentValToHeight(currentVal, maxVal);

        bool draw = false;
        if(lastCoolantHeight != currentHeight) {
            lastCoolantHeight = currentHeight;
            draw = true;
        }

        if(overheat) {
            draw = true;
            color = (alertSwitch()) ? ST7735_RED : ST77XX_ORANGE;
        }

        if(draw) {
            x = t_getBaseX() + 16;
            y = t_getBaseY() + 4 + (TEMP_BAR_MAXHEIGHT - currentHeight);

            drawTempBar(x, y, currentHeight, color);

            x = t_getBaseX() + TEMP_DOT_X;
            y = t_getBaseY() + TEMP_DOT_Y;

            tft.fillCircle(x, y, 6, color);
        }

        if(lastCoolantVal != valToDisplay) {
            lastCoolantVal = valToDisplay;

            x = t_getBaseX() + 26;
            y = t_getBaseY() + 19;
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
            color = ST77XX_ORANGE;
        } 
        if(currentVal > TEMP_OIL_OK_HI) {
            overheat = true;
        }

        currentVal -= TEMP_MIN;
        maxVal -= TEMP_MIN;
        if(currentVal < 0) {
            currentVal = 0;
        }

        int currentHeight = currentValToHeight(currentVal, maxVal);

        bool draw = false;
        if(lastOilHeight != currentHeight) {
            lastOilHeight = currentHeight;
            draw = true;
        }

        if(overheat) {
            draw = true;
            color = (alertSwitch()) ? ST7735_RED : ST77XX_ORANGE;
        }

        if(draw) {
            x = o_getBaseX() + 14;
            y = o_getBaseY() + 4 + (TEMP_BAR_MAXHEIGHT - currentHeight);

            drawTempBar(x, y, currentHeight, color);

            x = o_getBaseX() + OIL_DOT_X;
            y = o_getBaseY() + OIL_DOT_Y;

            tft.fillCircle(x, y, 6, color);
        }

        if(lastOilVal != valToDisplay) {
            lastOilVal = valToDisplay;

            x = o_getBaseX() + 25;
            y = o_getBaseY() + 19;
            drawTempValue(x, y, valToDisplay);
        }
    }
}

//-------------------------------------------------------------------------------------------------
//pressure indicator
//-------------------------------------------------------------------------------------------------

static bool p_drawOnce = true; 
void redrawPressure(void) {
    p_drawOnce = true;
}

const int p_getBaseX(void) {
    return (BIG_ICONS_WIDTH * 2);
}

const int p_getBaseY(void) {
    return 0; 
}

static int lastHI = C_INIT_VAL;
static int lastLO = C_INIT_VAL;

void showPressureAmount(float current) {

    int x, y;

    if(p_drawOnce) {
        drawImage(p_getBaseX(), p_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, BIG_ICONS_BG_COLOR, (unsigned short*)pressure);
        x = p_getBaseX() + BIG_ICONS_WIDTH;
        tft.drawLine(x, p_getBaseY(), x, BIG_ICONS_HEIGHT, BIG_ICONS_BG_COLOR);

        p_drawOnce = false;
    } else {

        int hi, lo;

        floatToDec(current, &hi, &lo);

        if(hi != lastHI || lo != lastLO) {
            lastHI = hi;
            lastLO = lo;

            memset(displayTxt, 0, sizeof(displayTxt));
            snprintf(displayTxt, sizeof(displayTxt) - 1, (const char*)F("%d.%d"), hi, lo);

            x = p_getBaseX() + BAR_TEXT_X;
            y = p_getBaseY() + BAR_TEXT_Y - 12;

            tft.fillRect(x, y, 28, 15, BIG_ICONS_BG_COLOR);

            x = p_getBaseX() + BAR_TEXT_X;
            y = p_getBaseY() + BAR_TEXT_Y;

            tft.setFont(&FreeSansBold9pt7b);
            tft.setTextSize(1);
            tft.setTextColor(TEXT_COLOR);
            tft.setCursor(x, y);
            tft.println(displayTxt);

            tft.setFont();
            tft.setTextSize(1);

            x = p_getBaseX() + 34;
            y = p_getBaseY() + 45;
            tft.setCursor(x, y);
            tft.println(F("BAR"));
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

int getThrottlePercentage(int currentVal) {
    float percent = (currentVal * 100) / PWM_RESOLUTION;
    return percentToGivenVal(percent, 100);
}

void showEngineLoadAmount(int currentVal) {

    int value = getThrottlePercentage(currentVal);

    if(e_drawOnce) {
        drawImage(e_getBaseX(), e_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, SMALL_ICONS_BG_COLOR, (unsigned short*)pump);
        e_drawOnce = false;
    } else {
        if(lastLoadAmount != value) {
            lastLoadAmount = value;

            int x, y, w, offset;

            tft.setFont();
            tft.setTextSize(1);
            tft.setTextColor(TEXT_COLOR);

            memset(displayTxt, 0, sizeof(displayTxt));
            snprintf(displayTxt, sizeof(displayTxt) - 1, (const char*)F("%d%%"), value);

            w = textWidth(displayTxt);

            x = e_getBaseX() + ((SMALL_ICONS_WIDTH - w) / 2);
            y = e_getBaseY() + 30;
            
            offset = 5;
            tft.fillRect(e_getBaseX() + offset, y, SMALL_ICONS_WIDTH - (offset * 2), 8, SMALL_ICONS_BG_COLOR);
            tft.setCursor(x, y);
            tft.println(displayTxt);
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

static int lastEGTTempVal = C_INIT_VAL;

void showEGTTemperatureAmount(int currentVal) {

    if(egt_drawOnce) {
        drawImage(egt_getBaseX(), egt_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, SMALL_ICONS_BG_COLOR, (unsigned short*)egt);
        egt_drawOnce = false;
    } else {
        if(lastEGTTempVal != currentVal) {
            lastEGTTempVal = currentVal;

            int x, y, w, offset;

            tft.setFont();
            tft.setTextSize(1);
            tft.setTextColor(TEXT_COLOR);

            memset(displayTxt, 0, sizeof(displayTxt));
            snprintf(displayTxt, sizeof(displayTxt) - 1, (const char*)F("%d"), currentVal);

            w = textWidth(displayTxt);

            x = egt_getBaseX() + (((SMALL_ICONS_WIDTH - w) / 2) - 2);
            y = egt_getBaseY() + 30;
            
            offset = 5;
            tft.fillRect(egt_getBaseX() + offset, y - 2, SMALL_ICONS_WIDTH - (offset * 2), 10, SMALL_ICONS_BG_COLOR);
            tft.setCursor(x, y);
            tft.println(displayTxt);

            tft.drawCircle(x + w + 2, y, 2, TEXT_COLOR);
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

            tft.setFont();
            tft.setTextSize(1);

            memset(displayTxt, 0, sizeof(displayTxt));

            bool error = currentVal < TEMP_LOWEST || currentVal > TEMP_HIGHEST;
            if(error) {
                tft.setTextColor(ST77XX_RED);
                snprintf(displayTxt, sizeof(displayTxt) - 1, (const char*)F("%s"), err);
            } else {
                tft.setTextColor(TEXT_COLOR);
                snprintf(displayTxt, sizeof(displayTxt) - 1, (const char*)F("%d"), currentVal);
            }

            w = textWidth(displayTxt);

            x = ic_getBaseX() + (((SMALL_ICONS_WIDTH - w) / 2) - 2);
            y = ic_getBaseY() + 30;
            
            offset = 5;
            tft.fillRect(ic_getBaseX() + offset, y - 2, SMALL_ICONS_WIDTH - (offset * 2), 10, SMALL_ICONS_BG_COLOR);
            tft.setCursor(x, y);
            tft.println(displayTxt);
            
            if(!error) {
                tft.drawCircle(x + w + 2, y, 2, TEXT_COLOR);
            }
        }
    }
}

//-------------------------------------------------------------------------------------------------
//engine rpm
//-------------------------------------------------------------------------------------------------

static bool rpm_drawOnce = true; 
void redrawRPM(void) {
    rpm_drawOnce = true;
}

const int rpm_getBaseX(void) {
    return SMALL_ICONS_WIDTH;
}

const int rpm_getBaseY(void) {
    return BIG_ICONS_HEIGHT; 
}

static int lastRPMAmount = C_INIT_VAL;
void showRPMamount(int currentVal) {

    if(rpm_drawOnce) {
        drawImage(rpm_getBaseX(), rpm_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, SMALL_ICONS_BG_COLOR, (unsigned short*)rpm);
        rpm_drawOnce = false;
    } else {
        if(lastRPMAmount != currentVal) {
            lastRPMAmount = currentVal;

            int x, y, w, offset;

            tft.setFont();
            tft.setTextSize(1);
            tft.setTextColor(TEXT_COLOR);

            memset(displayTxt, 0, sizeof(displayTxt));
            snprintf(displayTxt, sizeof(displayTxt) - 1, (const char*)F("%d"), currentVal);

            w = textWidth(displayTxt);

            x = rpm_getBaseX() + ((SMALL_ICONS_WIDTH - w) / 2);
            y = rpm_getBaseY() + 30;
            
            offset = 5;
            tft.fillRect(rpm_getBaseX() + offset, y, SMALL_ICONS_WIDTH - (offset * 2), 8, SMALL_ICONS_BG_COLOR);
            tft.setCursor(x, y);
            tft.println(displayTxt);
        }
    }
}

//-------------------------------------------------------------------------------------------------
//fuel indicator
//-------------------------------------------------------------------------------------------------

static bool f_drawOnce = true; 
void redrawFuel(void) {
    f_drawOnce = true;
}

static int currentFuelWidth = 0;
static bool fullRedrawNeeded = false;

int f_getBaseX(void) {
    return (OFFSET * 3) + FUEL_WIDTH + OFFSET;
}

int f_getBaseY(void) {
    return SCREEN_H - FUEL_HEIGHT - textHeight(empty) - OFFSET; 
}

int f_getWidth(void) {
    return SCREEN_W - f_getBaseX() - (OFFSET / 2);
}

void drawFuelEmpty(void) {
    if(currentFuelWidth <= 1) {

        int color = ST7735_WHITE;
        if(seriousAlertSwitch()) {
            color = ST7735_RED;
        }

        int x = f_getBaseX() + ((f_getWidth() - textWidth(emptyMessage)) / 2);
        int y = f_getBaseY() + ((FUEL_HEIGHT - textHeight(emptyMessage)) / 2);

        tft.setTextSize(1);
        tft.setTextColor(color);
        tft.setCursor(x, y);
        tft.println(emptyMessage);
    }
}

void showFuelAmount(int currentVal, int maxVal) {
    int width = f_getWidth();
    float percent = (currentVal * 100) / maxVal;
    currentFuelWidth = percentToGivenVal(percent, width);
    if(currentFuelWidth > width) {
        currentFuelWidth = width;
    }
    if(currentFuelWidth <= 1 && !fullRedrawNeeded) {
        fullRedrawNeeded = true;
    }

    if(f_drawOnce) {
        int x = 0; 
        int y = y = f_getBaseY();
        int tw;

        tft.fillRect(x, y, SCREEN_W, SCREEN_H - y, ST7735_BLACK);

        x = f_getBaseX();

        drawImage(x - FUEL_WIDTH - OFFSET, y, FUEL_WIDTH, FUEL_HEIGHT, 0, (unsigned short*)fuelIcon);
        tft.drawRect(x, y, width, FUEL_HEIGHT, FUEL_BOX_COLOR);

        tft.fillTriangle(x - 6 - FUEL_WIDTH - OFFSET, 
            y + ((FUEL_HEIGHT) / 2), 
            x - FUEL_WIDTH - OFFSET - 1, 
            y + 6, 
            x - FUEL_WIDTH - OFFSET - 1, 
            y + (FUEL_HEIGHT - 6), 
            FUEL_COLOR);

        drawChangeableFuelContent(currentFuelWidth);

        y += FUEL_HEIGHT + (OFFSET / 2);

        tft.setTextSize(1);
        tft.setTextColor(ST7735_RED);
        tft.setCursor(x, y);
        tft.println(empty);

        tw = textWidth(half);
        x = f_getBaseX();
        x += ((width - tw) / 2);

        tft.setTextColor(TEXT_COLOR);
        tft.setCursor(x, y);
        tft.println(half);

        x = f_getBaseX() + width;
        tw = textWidth(full);
        x -= tw;

        tft.setCursor(x, y);
        tft.println(full);

        f_drawOnce = false;
    } else {
        drawChangeableFuelContent(currentFuelWidth);
    }
}

static int lastWidth = 0;

void drawChangeableFuelContent(int w) {

    bool draw = false;
    if(lastWidth != w) {
        lastWidth = w;
        draw = true;
    }

    int color = FUEL_FILL_COLOR;

    int width = f_getWidth();
    int minW = percentToGivenVal(MINIMUM_FUEL_AMOUNT_PERCENTAGE, width);
    if(w <= minW && w >= 1) {
        draw = true;
        if(alertSwitch()) {
            color = ST7735_RED;
        }
    }

    if(draw) {
        int x = f_getBaseX(), y = f_getBaseY(); 

        if(fullRedrawNeeded) {
            tft.fillRect(x + 1, y + 1, width - 2, FUEL_HEIGHT - 2, ST7735_BLACK);
            fullRedrawNeeded = false;
        }

        tft.fillRect(x, y + 1, w, FUEL_HEIGHT - 2, color);
        int toFill =  width - w - 1;
        if(toFill < 0) {
            toFill = 0;
        }
        tft.fillRect(x + w, y + 1, toFill, FUEL_HEIGHT - 2, ST7735_BLACK);
        tft.drawRect(x, y, width, FUEL_HEIGHT, FUEL_BOX_COLOR);
    }
}

//-------------------------------------------------------------------------------------------------
//volt indicator
//-------------------------------------------------------------------------------------------------

int v_getBaseX(void) {
    return 0;
}

int v_getBaseY(void) {
    return 120; 
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
            color = VOLTS_BIG_ERROR_COLOR;
        }

        tft.setTextColor(color);
        tft.setCursor(x, y);

        memset(displayTxt, 0, sizeof(displayTxt));
        snprintf(displayTxt, sizeof(displayTxt) - 1, (const char*)F("%d.%dv"), v1, v2);

        tft.fillRect(x, y, textWidth((const char*)F("14.4v")) + 2, 8, ST7735_BLACK);

        tft.setTextSize(1);
        tft.println(displayTxt);
    }
}

//-------------------------------------------------------------------------------------------------
//READERS
//-------------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------------
//Read volts
//-------------------------------------------------------------------------------------------------

float readVolts(void) {
    return analogRead(A2) / DIVIDER_VOLTS;
}

//-------------------------------------------------------------------------------------------------
//Read coolant temperature
//-------------------------------------------------------------------------------------------------

float readCoolantTemp(void) {
    set4051ActivePin(0);
    return ntcToTemp(A1, 1506, 1500);
}

//-------------------------------------------------------------------------------------------------
//Read oil temperature
//-------------------------------------------------------------------------------------------------

float readOilTemp(void) {
    set4051ActivePin(1);
    return ntcToTemp(A1, 1506, 1500);
}

//-------------------------------------------------------------------------------------------------
//Read throttle
//-------------------------------------------------------------------------------------------------

static int lastThrottle = 0;
float readThrottle(void) {
    set4051ActivePin(2);

    float rawVal = getAverageValueFrom(A1);
    float initialVal = rawVal - THROTTLE_MIN;

    if(initialVal < 0) {
        initialVal = 0;
    }
    int maxVal = (THROTTLE_MAX - THROTTLE_MIN);

    if(initialVal > maxVal) {
        initialVal = maxVal;
    }
    float divider = maxVal / (float)PWM_RESOLUTION;
    int result = (initialVal / divider);
    result = abs(result - PWM_RESOLUTION);

    int debugValue = getThrottlePercentage(result);
    if(lastThrottle != debugValue) {
        lastThrottle = debugValue;
        sendThrottleValueCAN(lastThrottle);
        deb("throttle: %d%%", debugValue);
    }

    return result;
}

//-------------------------------------------------------------------------------------------------
//Read air temperature
//-------------------------------------------------------------------------------------------------

float readAirTemperature(void) {
    set4051ActivePin(3);
    return ntcToTemp(A1, 5050, 4700);
}

//-------------------------------------------------------------------------------------------------
//Read bar pressure amount
//-------------------------------------------------------------------------------------------------

float readBarPressure(void) {
    set4051ActivePin(5);

    float val = ((float)analogRead(A1) / DIVIDER_PRESSURE_BAR) - 1.0;
    if(val < 0.0) {
        val = 0.0;
    } 
    return val;
}

//-------------------------------------------------------------------------------------------------
//Read EGT temperature
//-------------------------------------------------------------------------------------------------

float readEGT(void) {
    set4051ActivePin(6);
    return (((float)getAverageValueFrom(A1)) / DIVIDER_EGT);
}
