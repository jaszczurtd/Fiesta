#include "engineFuel.h"

//-------------------------------------------------------------------------------------------------
//Read fuel amount
//-------------------------------------------------------------------------------------------------

EngineFuelGauge fuel_g = EngineFuelGauge();

void redrawFuel(void) {
  fuel_g.redraw();
}

void drawFuelEmpty(void) {
  fuel_g.drawFuelEmpty();
}

void showFuelAmount(void) {
  fuel_g.showFuelAmount((int)valueFields[F_FUEL], FUEL_MIN - FUEL_MAX);
}

void initFuelMeasurement(void) {
  fuel_g.init();
}

EngineFuelGauge::EngineFuelGauge(void) { }

void EngineFuelGauge::init() {
  memset(measuredValues, FUEL_INIT_VALUE, sizeof(measuredValues));
  measuedValuesIndex = 0;
  lastResult = FUEL_INIT_VALUE;

  fuelMeasurementTime = FUEL_MEASUREMENT_TIME_START;
  nextMeasurement = getSeconds() + fuelMeasurementTime;
  measurements = 0;

  hal_display_set_default_font();
  hal_display_get_text_bounds(emptyMessage, &emptyMessageWidth, &emptyMessageHeight);

  currentFuelWidth = 0;
  fullRedrawNeeded = false;
  lastWidth = 0;
}

//-------------------------------------------------------------------------------------------------
//fuel indicator
//-------------------------------------------------------------------------------------------------

void EngineFuelGauge::redraw(void) {
    f_drawOnce = true;
}

int EngineFuelGauge::getBaseX(void) {
    return (OFFSET * 2) + FUEL_WIDTH + 4;
}

int EngineFuelGauge::getBaseY(void) {
    return SCREEN_H - FUEL_HEIGHT - 8 - (OFFSET * 2); 
}

int EngineFuelGauge::getWidth(void) {
    return FUEL_GAUGE_WIDTH - getBaseX();
}

int EngineFuelGauge::getGaugePos(void) {
  int center = ((FUEL_HEIGHT - FUEL_GAUGE_HEIGHT) / 2);
  if(center < 0) {
    center = 0;
  }
  return getBaseY() + center;
}

void EngineFuelGauge::drawFuelEmpty(void) {

  if(lastResult == FUEL_INIT_VALUE) {
    return;
  }

  if(currentFuelWidth <= MIN_FUEL_WIDTH) {

    int color = HAL_COLOR(WHITE);
    if(seriousAlertSwitch()) {
        color = HAL_COLOR(RED);
    }

    int x = getBaseX() + ((getWidth() - emptyMessageWidth) / 2);
    int y = getBaseY() + ((FUEL_HEIGHT - emptyMessageHeight) / 2);

    hal_display_set_default_font_with_pos_and_color(x, y, (uint16_t)color);
    hal_display_println(emptyMessage);
  }
}

void EngineFuelGauge::showFuelAmount(int currentVal, int maxVal) {
  int width = getWidth();
  float percent = (currentVal * 100) / maxVal;
  currentFuelWidth = percentToGivenVal(percent, width);
  if(currentFuelWidth > width) {
      currentFuelWidth = width;
  }

  if(currentFuelWidth <= MIN_FUEL_WIDTH && !fullRedrawNeeded) {
      fullRedrawNeeded = true;
  }

  int center = ((FUEL_HEIGHT - FUEL_GAUGE_HEIGHT) / 2);
  if(center < 0) {
    center = 0;
  }

  if(f_drawOnce) {
    int x = 0; 
    int y = getBaseY();
    int tw;

    hal_display_fill_rect(FUEL_WIDTH + OFFSET + (OFFSET / 2),
                y,
                getWidth() + OFFSET, SCREEN_H - y,
                ICONS_BG_COLOR);

    x = getBaseX();

    hal_display_draw_image(x - FUEL_WIDTH - OFFSET, y, FUEL_WIDTH, FUEL_HEIGHT, 0, (uint16_t*)fuelIcon);

    y = getGaugePos();

    hal_display_draw_rect(x, y, width, FUEL_GAUGE_HEIGHT, FUEL_BOX_COLOR);

    drawChangeableFuelContent(currentFuelWidth, FUEL_GAUGE_HEIGHT, y);

    y += FUEL_GAUGE_HEIGHT + (OFFSET / 2);

    hal_display_set_default_font_with_pos_and_color(x, y, HAL_COLOR(RED));
    hal_display_println(empty);

    tw = hal_display_text_width(half);
    x = getBaseX();
    x += ((width - tw) / 2);

    hal_display_set_text_color(TEXT_COLOR);
    hal_display_print_at(x, y, half);

    x = getBaseX() + width;
    tw = hal_display_text_width(full);
    x -= tw;

    hal_display_print_at(x, y, full);

    f_drawOnce = false;
  } else {
    drawChangeableFuelContent(currentFuelWidth, FUEL_GAUGE_HEIGHT, getGaugePos());
  }
  drawFuelEmpty();
}

void EngineFuelGauge::drawChangeableFuelContent(int w, int fh, int y) {

    bool draw = false;
    if(lastWidth != w) {
        lastWidth = w;
        draw = true;
    }

    int color = FUEL_FILL_COLOR;

    int width = getWidth();
    int minW = percentToGivenVal(MINIMUM_FUEL_AMOUNT_PERCENTAGE, width);
    if(w <= minW && w >= 1) {
        draw = true;
        if(alertSwitch()) {
            color = HAL_COLOR(RED);
        }
    }

    if(draw) {
      int x = getBaseX(); 

      if(fullRedrawNeeded) {
          hal_display_fill_rect(x + 1, y + 1, width - 2, fh - 2, HAL_COLOR(BLACK));
          fullRedrawNeeded = false;
      }

      hal_display_fill_rect(x, y + 1, w, fh - 2, color);
      int toFill =  width - w - 1;
      if(toFill < 0) {
          toFill = 0;
      }
      hal_display_fill_rect(x + w, y + 1, toFill, fh - 2, HAL_COLOR(BLACK));
      hal_display_draw_rect(x, y, width, fh, FUEL_BOX_COLOR);
    }
}
