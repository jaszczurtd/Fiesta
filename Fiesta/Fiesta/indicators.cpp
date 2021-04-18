#include "indicators.h"

//-------------------------------------------------------------------------------------------------
//coolant temperature indicator
//-------------------------------------------------------------------------------------------------

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

void showTemperatureAmount(int currentVal, int maxVal) {

    if(t_drawOnce) {
        drawImage(t_getBaseX(), t_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, BIG_ICONS_BG_COLOR, (unsigned int*)temperature);
        t_drawOnce = false;
    } else {
        int x, y, color;

        int valToDisplay = currentVal;
        if(currentVal > TEMP_MAX) {
            currentVal = TEMP_MAX;
        }

        bool overheat = false;
        color = ST7735_BLUE;
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
            Adafruit_ST7735 tft = returnReference();

            x = t_getBaseX() + 16;
            y = t_getBaseY() + 4 + (TEMP_BAR_MAXHEIGHT - currentHeight);

            drawTempBar(x, y, currentHeight, color);

            x = t_getBaseX() + TEMP_DOT_X;
            y = t_getBaseY() + TEMP_DOT_Y;

            tft.fillCircle(x, y, 6, color);

        }
        x = t_getBaseX() + 26;
        y = t_getBaseY() + 19;
        drawTempValue(x, y, valToDisplay);
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

void showOilAmount(int currentVal, int maxVal) {

    if(o_drawOnce) {
        drawImage(o_getBaseX(), o_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, 0xf7be, (unsigned int*)oil);
        o_drawOnce = false;
    } else {
        int x, y, color;

        int valToDisplay = currentVal;
        if(currentVal > TEMP_OIL_MAX) {
            currentVal = TEMP_OIL_MAX;
        }

        bool overheat = false;
        color = ST7735_BLUE;
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
            Adafruit_ST7735 tft = returnReference();

            x = o_getBaseX() + 14;
            y = o_getBaseY() + 4 + (TEMP_BAR_MAXHEIGHT - currentHeight);

            drawTempBar(x, y, currentHeight, color);

            x = o_getBaseX() + OIL_DOT_X;
            y = o_getBaseY() + OIL_DOT_Y;

            tft.fillCircle(x, y, 6, color);
        }
        
        x = o_getBaseX() + 25;
        y = o_getBaseY() + 19;
        drawTempValue(x, y, valToDisplay);
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

    Adafruit_ST7735 tft = returnReference();
    int x, y;

    if(p_drawOnce) {
        drawImage(p_getBaseX(), p_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, BIG_ICONS_BG_COLOR, (unsigned int*)pressure);
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
            snprintf(displayTxt, sizeof(displayTxt) - 1, "%d.%d", hi, lo);

            x = p_getBaseX() + BAR_TEXT_X;
            y = p_getBaseY() + BAR_TEXT_Y - 11;

            tft.fillRect(x, y, 28, 13, BIG_ICONS_BG_COLOR);

            x = p_getBaseX() + BAR_TEXT_X;
            y = p_getBaseY() + BAR_TEXT_Y;

            tft.setFont(&FreeSansBold9pt7b);
            tft.setTextSize(1);
            tft.setTextColor(ST7735_BLACK);
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

void showEngineLoadAmount(unsigned char currentVal) {

    if(e_drawOnce) {
        drawImage(e_getBaseX(), e_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, SMALL_ICONS_BG_COLOR, (unsigned int*)pump);
        e_drawOnce = false;
    } else {
        if(lastLoadAmount != currentVal) {
            lastLoadAmount = currentVal;

            int x, y, w, offset;
            Adafruit_ST7735 tft = returnReference();        

            tft.setFont();
            tft.setTextSize(1);
            tft.setTextColor(ST7735_BLACK);

            memset(displayTxt, 0, sizeof(displayTxt));
            snprintf(displayTxt, sizeof(displayTxt) - 1, "%d%%", currentVal);

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
        drawImage(egt_getBaseX(), egt_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, SMALL_ICONS_BG_COLOR, (unsigned int*)egt);
        egt_drawOnce = false;
    } else {
        if(lastEGTTempVal != currentVal) {
            lastEGTTempVal = currentVal;

            int x, y, w, offset;
            Adafruit_ST7735 tft = returnReference();        

            tft.setFont();
            tft.setTextSize(1);
            tft.setTextColor(ST7735_BLACK);

            memset(displayTxt, 0, sizeof(displayTxt));
            snprintf(displayTxt, sizeof(displayTxt) - 1, "%d", currentVal);

            w = textWidth(displayTxt);

            x = egt_getBaseX() + (((SMALL_ICONS_WIDTH - w) / 2) - 2);
            y = egt_getBaseY() + 30;
            
            offset = 5;
            tft.fillRect(egt_getBaseX() + offset, y - 2, SMALL_ICONS_WIDTH - (offset * 2), 10, SMALL_ICONS_BG_COLOR);
            tft.setCursor(x, y);
            tft.println(displayTxt);

            tft.drawCircle(x + w + 2, y, 2, ST7735_BLACK);
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

void showICTemperatureAmount(unsigned char currentVal) {

    if(ic_drawOnce) {
        drawImage(ic_getBaseX(), ic_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, SMALL_ICONS_BG_COLOR, (unsigned int*)ic);
        ic_drawOnce = false;
    } else {
        if(lastICTempVal != currentVal) {
            lastICTempVal = currentVal;

            int x, y, w, offset;
            Adafruit_ST7735 tft = returnReference();        

            tft.setFont();
            tft.setTextSize(1);
            tft.setTextColor(ST7735_BLACK);

            memset(displayTxt, 0, sizeof(displayTxt));
            snprintf(displayTxt, sizeof(displayTxt) - 1, "%d", currentVal);

            w = textWidth(displayTxt);

            x = ic_getBaseX() + (((SMALL_ICONS_WIDTH - w) / 2) - 2);
            y = ic_getBaseY() + 30;
            
            offset = 5;
            tft.fillRect(ic_getBaseX() + offset, y - 2, SMALL_ICONS_WIDTH - (offset * 2), 10, SMALL_ICONS_BG_COLOR);
            tft.setCursor(x, y);
            tft.println(displayTxt);

            tft.drawCircle(x + w + 2, y, 2, ST7735_BLACK);
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
        drawImage(rpm_getBaseX(), rpm_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, SMALL_ICONS_BG_COLOR, (unsigned int*)rpm);
        rpm_drawOnce = false;
    } else {
        if(lastRPMAmount != currentVal) {
            lastRPMAmount = currentVal;

            int x, y, w, offset;
            Adafruit_ST7735 tft = returnReference();        

            tft.setFont();
            tft.setTextSize(1);
            tft.setTextColor(ST7735_BLACK);

            memset(displayTxt, 0, sizeof(displayTxt));
            snprintf(displayTxt, sizeof(displayTxt) - 1, "%d", currentVal);

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

const char *half = (char*)F("1/2");
const char *full = (char*)F("F");
const char *empty = (char*)F("E");
const char *emptyMessage = (char*)F("Pusty bak!");

static bool f_drawOnce = true; 
void redrawFuel(void) {
    f_drawOnce = true;
}

static int currentWidth = 0;

int f_getBaseX(void) {
    return OFFSET + FUEL_WIDTH + OFFSET;
}

int f_getBaseY(void) {
    return SCREEN_H - FUEL_HEIGHT - textHeight(empty) - (OFFSET / 2); 
}

int f_getWidth(void) {
    return SCREEN_W - f_getBaseX() - OFFSET;
}

void drawFuelEmpty(void) {
    if(currentWidth <= 1) {

        int color = ST7735_WHITE;
        if(seriousAlertSwitch()) {
            color = ST7735_RED;
        }

        int x = f_getBaseX() + ((f_getWidth() - textWidth(emptyMessage)) / 2);
        int y = f_getBaseY() + ((FUEL_HEIGHT - textHeight(emptyMessage)) / 2);

        Adafruit_ST7735 tft = returnReference();
        tft.setTextSize(1);
        tft.setTextColor(color);
        tft.setCursor(x, y);
        tft.println(emptyMessage);
    }
}

void showFuelAmount(int currentVal, int maxVal) {
    int width = f_getWidth();
    float percent = (currentVal * 100) / maxVal;
    currentWidth = percentToWidth(percent, width);

    if(f_drawOnce) {
        Adafruit_ST7735 tft = returnReference();

        int x = f_getBaseX(), y = f_getBaseY(), tw;

        drawImage(OFFSET, y, FUEL_WIDTH, FUEL_HEIGHT, 0, (unsigned int*)fuelIcon);

        drawChangeableFuelContent(currentWidth);

        y += FUEL_HEIGHT + (OFFSET / 2);

        tft.setTextSize(1);
        tft.setTextColor(ST7735_RED);
        tft.setCursor(x, y);
        tft.println(empty);

        tw = textWidth(half);
        x = f_getBaseX();
        x += ((width - tw) / 2);

        tft.setTextColor(ST7735_WHITE);
        tft.setCursor(x, y);
        tft.println(half);

        x = f_getBaseX() + width;
        tw = textWidth(full);
        x -= tw;

        tft.setCursor(x, y);
        tft.println(full);

        f_drawOnce = false;
    } else {
        drawChangeableFuelContent(currentWidth);
    }
}

static int lastWidth = 0;

void drawChangeableFuelContent(int w) {

    bool draw = false;
    if(lastWidth != w) {
        lastWidth = w;
        draw = true;
    }

    int color = C_GRAY_MEDIUM;

    int width = f_getWidth();
    int minW = percentToWidth(MINIMUM_FUEL_AMOUNT_PERCENTAGE, width);
    if(w <= minW) {
        draw = true;
        if(alertSwitch()) {
            color = ST7735_RED;
        }
    }

    if(draw) {
        Adafruit_ST7735 tft = returnReference();        

        int x = f_getBaseX(), y = f_getBaseY(); 
        tft.fillRect(x, y, w, FUEL_HEIGHT, color);
        tft.drawLine(x + w, y, x + w, y + FUEL_HEIGHT, ST7735_BLACK);
        tft.drawRect(x, y, width, FUEL_HEIGHT, FUEL_BOX_COLOR);
    }
}

//-------------------------------------------------------------------------------------------------
//READERS
//-------------------------------------------------------------------------------------------------


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

float readThrottle(void) {
    set4051ActivePin(2);
    return ((getAverageValueFrom(A1) - THROTTLE_MIN) * 100) / (THROTTLE_MAX - THROTTLE_MIN);
}

//-------------------------------------------------------------------------------------------------
//Read air temperature
//-------------------------------------------------------------------------------------------------

float readAirTemperature(void) {
    set4051ActivePin(3);
    return ntcToTemp(A1, 5050, 5100);
}
