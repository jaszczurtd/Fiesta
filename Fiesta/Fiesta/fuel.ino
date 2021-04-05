
#include <Arduino.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789

#include "graphics.h"

#define FUEL_X 4
#define FUEL_Y 2

#define FUEL_WIDTH 18
#define FUEL_HEIGHT 18

#define OFFSET 4

void showFuelAmount(void) {
    int x, y, w, h;

    Adafruit_ST7735 tft = returnReference();

    drawImage(FUEL_X, FUEL_Y, FUEL_WIDTH, FUEL_HEIGHT, (unsigned int*)fuel);

    x = FUEL_X + FUEL_WIDTH + OFFSET;
    y = FUEL_Y;
    h = FUEL_HEIGHT;

    w = 40;
    tft.fillRect(x, y, w, h, C_GRAY_MEDIUM);

    w = SCREEN_W - x - OFFSET;
    tft.drawRect(x, y, w, h, C_GRAY_DARK);


}