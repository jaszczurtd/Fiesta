
#include "graphics.h"

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Adafruit_ST7735 returnReference(void) {
  return tft;
}

void initGraphics(void) {

  // Use this initializer if using a 1.8" TFT screen:
  tft.initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
  tft.setRotation(1);

  tft.fillScreen(ST7735_WHITE);

  int x = (SCREEN_W - FIESTA_LOGO_WIDTH) / 2;
  int y = (SCREEN_H - FIESTA_LOGO_HEIGHT) / 2;
  drawImage(x, y, FIESTA_LOGO_WIDTH, FIESTA_LOGO_HEIGHT, 0xffff, (unsigned int*)FiestaLogo);
}

void drawImage(int x, int y, int width, int height, int background, unsigned int *pointer) {
    tft.fillRect(x, y, width, height, background);

    for(register int row = 0; row < height; row++) {
        for(register int col = 0; col < width; col++) {
            int px = pgm_read_word(pointer++);
            if(px != background) {
                tft.drawPixel(col + x, row + y, px);
            }
        }      
    }
}

int textWidth(const char* text) {
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return w;
}

int textHeight(const char* text) {
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return h;
}

void drawTempValue(int x, int y, int valToDisplay) {
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(ST7735_BLACK);
    tft.setCursor(x, y);

    tft.fillRect(x, y, 24, 8, BIG_ICONS_BG_COLOR);

    if(valToDisplay < TEMP_LOWEST || valToDisplay > TEMP_HIGHEST) {
        tft.println(err);
        return;
    } else {
        char temp[8];
        memset(temp, 0, sizeof(temp));

        snprintf(temp, sizeof(temp) - 1, (const char*)F("%d"), valToDisplay);
        tft.println(temp);
    }
}

void drawTempBar(int x, int y, int currentHeight, int color) {
    tft.fillRect(x, y, 3, currentHeight, color);
    tft.fillRect(x, y - 2, 3, 2, BIG_ICONS_BG_COLOR);
}

void displayErrorWithMessage(int x, int y, const char *msg) {
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


