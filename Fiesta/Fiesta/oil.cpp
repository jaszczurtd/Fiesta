
#include "oil.h"


//-------------------------------------------------------------------------------------------------
//oil temperature indicator
//-------------------------------------------------------------------------------------------------

static bool o_drawOnce = true; 
void redrawOil(void) {
    o_drawOnce = true;
}

const int o_getBaseX(void) {
    return BIG_ICONS_WIDTH;
}

const int o_getBaseY(void) {
    return 0; 
}

static int lastOilHeight = C_INIT_VAL;
static int lastOilVal = C_INIT_VAL;

void showOilAmount(int currentVal, int maxVal) {

    if(o_drawOnce) {
        drawImage(o_getBaseX(), o_getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, 0xf7be, (unsigned short*)oil);
        o_drawOnce = false;
    } else {
        int x, y, color;
        TFT tft = returnReference();

        int valToDisplay = currentVal;
        if(currentVal > TEMP_OIL_MAX) {
            currentVal = TEMP_OIL_MAX;
        }

        bool overheat = false;
        color = TEMP_INITIAL_COLOR;
        if(currentVal >= TEMP_OK_LO && currentVal <= TEMP_OIL_OK_HI) {
            color = COLOR(ORANGE);
        } 
        if(currentVal > TEMP_OIL_OK_HI) {
            overheat = true;
        }

        bool blink = alertSwitch();
        if(currentVal > TEMP_OIL_OK_HI + ((TEMP_OIL_MAX - TEMP_OIL_OK_HI) / 2)) {
          blink = seriousAlertSwitch();
        }

        currentVal -= TEMP_MIN;
        maxVal -= TEMP_MIN;
        if(currentVal < 0) {
            currentVal = 0;
        }

        int currentHeight = currentValToHeight(
            (currentVal < TEMP_OIL_MAX) ? currentVal : TEMP_OIL_MAX,
            maxVal);

        bool draw = false;
        if(lastOilHeight != currentHeight) {
            lastOilHeight = currentHeight;
            draw = true;
        }

        if(overheat) {
            draw = true;
            color = (blink) ? COLOR(RED) : COLOR(ORANGE);
        }

        if(draw) {
            x = o_getBaseX() + 20;
            y = o_getBaseY() + 8 + TEMP_BAR_MAXHEIGHT;

            drawTempBar(x, y, currentHeight, color);

            x = o_getBaseX() + OIL_DOT_X;
            y = o_getBaseY() + OIL_DOT_Y;

            tft.fillCircle(x, y, TEMP_BAR_DOT_RADIUS, color);
        }

        if(lastOilVal != valToDisplay) {
            lastOilVal = valToDisplay;

            x = o_getBaseX() + 36;
            y = o_getBaseY() + 38;
            drawTempValue(x, y, valToDisplay);
        }
    }
}

//-------------------------------------------------------------------------------------------------
//oil pressure indicator
//-------------------------------------------------------------------------------------------------

static bool op_drawOnce = true; 
void redrawOilPressure(void) {
    op_drawOnce = true;
}

const int op_getBaseX(void) {
    return BIG_ICONS_WIDTH;
}

const int op_getBaseY(void) {
    return 0; 
}

void showOilPressureAmount(float current) {

}
