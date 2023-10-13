
#include "tempGauge.h"

TempGauge::TempGauge(int mode, TFT& tft) : tft(tft), mode(mode) {
}

void TempGauge::drawTempBar(int x, int y, int currentHeight, int color) {
  tft.fillRect(x, y, TEMP_BAR_WIDTH, -currentHeight, color);
  tft.fillRect(x, y - currentHeight, TEMP_BAR_WIDTH, 
      -(TEMP_BAR_MAXHEIGHT - currentHeight), ICONS_BG_COLOR);
}