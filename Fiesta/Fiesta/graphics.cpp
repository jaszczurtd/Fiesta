
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
  delay(2000);
  tft.fillScreen(ST7735_BLACK);
}

