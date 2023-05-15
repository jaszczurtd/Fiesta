
#include "tests.h"

bool initTests(void) {

  //tbd
  return true;
}

bool startTests(void) {

  

  //tbd
  return true;
}

#ifdef DEBUG_SCREEN
void debugFunc(void) {

  Adafruit_ST7735 tft = returnReference();

  int x = 0;
  int y = 0;
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(x, y); y += 9;
  tft.println(glowPlugsTime);

}
#endif
