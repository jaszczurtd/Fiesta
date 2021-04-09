
#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789

#include "graphics.h"

#define MINIMUM_FUEL_AMOUNT_PERCENTAGE 10

#define OFFSET 4

const char *half = (char*)"1/2";
const char *full = (char*)"F";
const char *empty = (char*)"E";
const char *emptyMessage = (char*)"Pusty bak!";

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
        draw = true;
    }

    int color = C_GRAY_MEDIUM;

    int width = f_getWidth();
    int minW = percentToWidth(MINIMUM_FUEL_AMOUNT_PERCENTAGE, width);
    if((w > minW) || alertSwitch()) {
        draw = true;
        color = ST7735_RED;
    }

    if(draw) {
        Adafruit_ST7735 tft = returnReference();        

        int x = f_getBaseX(), y = f_getBaseY(); 
        tft.fillRect(x, y, w, FUEL_HEIGHT, color);
        tft.drawLine(x + w, y, x + w, y + FUEL_HEIGHT, ST7735_BLACK);
        tft.drawRect(x, y, width, FUEL_HEIGHT, C_GRAY_DARK);
    }
}

