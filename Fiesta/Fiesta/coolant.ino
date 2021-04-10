
#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789

#include "graphics.h"
#include "temperature.h"

#define TEMP_DOT_X 17
#define TEMP_DOT_Y 39

static bool t_drawOnce = true; 
void redrawTemperature(void) {
    t_drawOnce = true;
}

const int t_getBaseX(void) {
    return BIG_ICONS_OFFSET;
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

