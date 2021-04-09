
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

void showOilAmount(int currentVal, int maxVal) {

    if(o_drawOnce) {
        drawImage(o_getBaseX(), o_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, 0xf7be, (unsigned int*)oil);
        o_drawOnce = false;
    } else {
        Adafruit_ST7735 tft = returnReference();

        int x, y;

        x = o_getBaseX() + OIL_DOT_X;
        y = o_getBaseY() + OIL_DOT_Y;

        tft.fillCircle(x, y, 6, ST7735_BLUE);




    }


}