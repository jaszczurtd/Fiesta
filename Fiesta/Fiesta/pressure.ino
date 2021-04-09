
#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789

#include <Fonts/FreeSansBold9pt7b.h>

#include "graphics.h"

#define BAR_TEXT_X 6
#define BAR_TEXT_Y 51

static bool p_drawOnce = true; 
void redrawPressure(void) {
    p_drawOnce = true;
}

const int p_getBaseX(void) {
    return  BIG_ICONS_OFFSET + (BIG_ICONS_WIDTH * 2);
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

        char bar[16];
        int hi, lo;
        int x, y;

        doubleToDec(current, &hi, &lo);

        if(hi != lastHI || lo != lastLO) {
            lastHI = hi;
            lastLO = lo;

            memset(bar, 0, sizeof(bar));
            snprintf(bar, sizeof(bar) - 1, "%d.%d", hi, lo);

            x = p_getBaseX() + BAR_TEXT_X;
            y = p_getBaseY() + BAR_TEXT_Y - 11;

            tft.fillRect(x, y, 28, 13, BIG_ICONS_BG_COLOR);

            x = p_getBaseX() + BAR_TEXT_X;
            y = p_getBaseY() + BAR_TEXT_Y;

            tft.setFont(&FreeSansBold9pt7b);
            tft.setTextSize(1);
            tft.setTextColor(ST7735_BLACK);
            tft.setCursor(x, y);
            tft.println(bar);

            tft.setFont();
            tft.setTextSize(1);

            x = p_getBaseX() + 34;
            y = p_getBaseY() + 45;
            tft.setCursor(x, y);
            tft.println("BAR");
        }
    }


}