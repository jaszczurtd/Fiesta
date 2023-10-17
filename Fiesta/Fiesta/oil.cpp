
#include "oil.h"


//-------------------------------------------------------------------------------------------------
//oil pressure indicator
//-------------------------------------------------------------------------------------------------

static bool op_drawOnce = true; 
void redrawOilPressure(void) {
    op_drawOnce = true;
}

const int op_getBaseX(void) {
    return (BIG_ICONS_WIDTH * 3);
}

const int op_getBaseY(void) {
    return BIG_ICONS_OFFSET; 
}

static int lastHI = C_INIT_VAL;
static int lastLO = C_INIT_VAL;

void showOilPressureAmount(float current) {
  TFT tft = returnTFTReference();
  
  if(op_drawOnce) {
    tft.drawImage(op_getBaseX(), op_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, ICONS_BG_COLOR, (unsigned short*)oilPressure);
    op_drawOnce = false;
  } else {
    int hi, lo;

    floatToDec(current, &hi, &lo);

    if(hi != lastHI || lo != lastLO) {
      lastHI = hi;
      lastLO = lo;
      tft.drawTextForPressureIndicators(op_getBaseX(), op_getBaseY(), (const char*)F("%d.%d"), hi, lo);
    }
  }
}

//-----------------------------------------------
// read oil pressure
//-----------------------------------------------

float readOilBarPressure(void) {
  return 0.0;
}
