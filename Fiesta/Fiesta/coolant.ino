
#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789

#include "graphics.h"

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

void showTemperatureAmount(int currentVal, int maxVal) {

    if(t_drawOnce) {
        drawImage(t_getBaseX(), t_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, 0xf7be, (unsigned int*)temperature);
        t_drawOnce = false;
    } else {
        Adafruit_ST7735 tft = returnReference();

        int x, y;

        x = t_getBaseX() + TEMP_DOT_X;
        y = t_getBaseY() + TEMP_DOT_Y;

        tft.fillCircle(x, y, 6, ST7735_BLUE);


    }


}