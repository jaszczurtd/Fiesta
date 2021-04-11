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

static int lastCoolantHeight = 0;

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
        if(currentVal <= TEMP_MIN && currentVal < TEMP_OK_LO) {
            color = ST7735_BLUE;
        } else 
        if(currentVal >= TEMP_OK_LO && currentVal < TEMP_OK_HI) {
            color = ST77XX_ORANGE;
        } else {
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

static int lastOilHeight = 0;

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
        if(currentVal <= TEMP_MIN && currentVal < TEMP_OK_LO) {
            color = ST7735_BLUE;
        } else 
        if(currentVal >= TEMP_OK_LO && currentVal < TEMP_OIL_OK_HI) {
            color = ST77XX_ORANGE;
        } else {
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

static int lastHI = 0, lastLO = 0;

void showPressureAmount(double current) {

    if(p_drawOnce) {
        drawImage(p_getBaseX(), p_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, BIG_ICONS_BG_COLOR, (unsigned int*)pressure);
        p_drawOnce = false;
    } else {
        Adafruit_ST7735 tft = returnReference();

        int hi, lo;
        int x, y;

        doubleToDec(current, &hi, &lo);

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

unsigned char lastICTempVal = 255;

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
            tft.fillRect(ic_getBaseX() + offset, y, SMALL_ICONS_WIDTH - (offset * 2), 8, SMALL_ICONS_BG_COLOR);
            tft.setCursor(x, y);
            tft.println(displayTxt);

            tft.drawCircle(x + w + 2, y, 2, ST7735_BLACK);
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
    double percent = (currentVal * 100) / maxVal;
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
        tft.drawRect(x, y, width, FUEL_HEIGHT, C_GRAY_DARK);
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

unsigned char lastLoadAmount = 255;

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

