#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <Arduino.h>

void displayErrorWithMessage(int x, int y, const char *msg) {
    Adafruit_ST7735 tft = returnReference();

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