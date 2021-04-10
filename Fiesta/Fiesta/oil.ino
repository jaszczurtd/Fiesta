
#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789

#include "graphics.h"

#define OIL_DOT_X 15
#define OIL_DOT_Y 39

static bool o_drawOnce = true; 
void redrawOil(void) {
    o_drawOnce = true;
}

const int o_getBaseX(void) {
    return BIG_ICONS_OFFSET + BIG_ICONS_WIDTH;
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

        Adafruit_ST7735 tft = returnReference();

        if(currentVal > TEMP_OIL_MAX) {
            currentVal = TEMP_OIL_MAX;
        }
        int valToDisplay = currentVal;

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

        double percent = (currentVal * 100) / maxVal;
        int currentHeight = percentToWidth(percent, TEMP_BAR_MAXHEIGHT);

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
            x = o_getBaseX() + 15;
            y = o_getBaseY() + 4 + (TEMP_BAR_MAXHEIGHT - currentHeight);

            tft.fillRect(x, y, 3, currentHeight, color);

            x = o_getBaseX() + OIL_DOT_X;
            y = o_getBaseY() + OIL_DOT_Y;

            tft.fillCircle(x, y, 6, color);

            tft.setFont();
            tft.setTextSize(1);
            tft.setTextColor(ST7735_BLACK);

            x = o_getBaseX() + 25;
            y = o_getBaseY() + 19;
            tft.setCursor(x, y);

            tft.fillRect(x, y, 22, 8, BIG_ICONS_BG_COLOR);

            char temp[8];
            memset(temp, 0, sizeof(temp));
            snprintf(temp, sizeof(temp) - 1, "%d", valToDisplay);

            tft.println(temp);
        }
    }
}