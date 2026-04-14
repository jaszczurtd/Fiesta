
#include "pressureGauge.h"

namespace {
constexpr int kBarTextX = 12;
constexpr int kBarTextY = 71;
}

PressureGauge turbop_g = PressureGauge(PRESSURE_G_TURBO);
PressureGauge oilp_g = PressureGauge(PRESSURE_G_OIL);

void redrawPressureGauges(void) {
  turbop_g.redraw();
  oilp_g.redraw();
}

void showPressureGauges(void) {
  turbop_g.showPressureGauge();
  oilp_g.showPressureGauge();
}

PressureGauge::PressureGauge(int mode) {
  this->mode = mode;
  drawOnce = true;
  lastHI = lastLO = lastHI_d = lastLO_d = C_INIT_VAL;
  lastAnimImg = NULL; 
}

void PressureGauge::drawTextForPressureIndicators(int x, int y, const char *format, ...) {
  char displayTxt[DISPLAY_TXT_SIZE];

  va_list valist;
  va_start(valist, format);
  hal_display_prepare_text_v(displayTxt, DISPLAY_TXT_SIZE, format, valist);
  va_end(valist);

  int x1 = x + kBarTextX;
  int y1 = y + kBarTextY - 12;

  hal_display_fill_rect(x1, y1, 28, 15, ICONS_BG_COLOR);

  x1 = x + kBarTextX;
  y1 = y + kBarTextY;

  hal_display_set_sans_bold_with_pos_and_color(x1, y1, TEXT_COLOR);
  hal_display_println(displayTxt);

  x1 = x + kBarTextX + 25;
  y1 = y + kBarTextY - 6;
  hal_display_set_default_font_with_pos_and_color(x1, y1, TEXT_COLOR);
  hal_display_println("BAR");
}

int PressureGauge::getBaseX(void) {
  switch(mode) {
    case PRESSURE_G_TURBO:
      return (BIG_ICONS_WIDTH * 2) + 6;
    case PRESSURE_G_OIL:
      return (BIG_ICONS_WIDTH * 3);
  }
  return -1;
}

int PressureGauge::getBaseY(void) {
  switch(mode) {
    case PRESSURE_G_TURBO:
    case PRESSURE_G_OIL:
      return BIG_ICONS_OFFSET; 
  }
  return -1;
}

void PressureGauge::redraw(void) {
  drawOnce = true;
}

void PressureGauge::showPressureGauge(void) {

  unsigned short *tempImg = NULL;
  int x, y, w;
  int hi, lo;
  float current = 0.0;

  if(drawOnce) {
    switch(mode) {
      case PRESSURE_G_TURBO:
        tempImg = (unsigned short*)turboPressure;
        break;
      case PRESSURE_G_OIL:
        tempImg = (unsigned short*)oilPressure;
        break;
    }

    hal_display_draw_image(getBaseX(), getBaseY(), BIG_ICONS_WIDTH, BIG_ICONS_HEIGHT, ICONS_BG_COLOR, tempImg);
    drawOnce = false;
  } else {
    switch(mode) {
      case PRESSURE_G_TURBO:
        current = valueFields[F_PRESSURE];
        break;
      case PRESSURE_G_OIL:
        current = getOilPressure();
        break;
    }

    floatToDec(current, &hi, &lo);
    if(hi != lastHI || lo != lastLO) {
      lastHI = hi;
      lastLO = lo;
      drawTextForPressureIndicators(getBaseX(), getBaseY(), (const char*)F("%d.%d"), hi, lo);
    }

    switch(mode) {
      case PRESSURE_G_TURBO:
        if(current > TURBO_MIN_PRESSURE_FOR_SPINNING) {
          unsigned short *img = NULL;
          bool draw = false;

          if(seriousAlertSwitch()) {
            img = (unsigned short*)pressure_a;
          } else {
            img = (unsigned short*)pressure_b;
          }

          if(img != lastAnimImg) {
            lastAnimImg = img;
            draw = true;
          }

          if(draw && img != NULL) {
            x = getBaseX() + PRESSURE_ICON_X;
            y = getBaseY() + PRESSURE_ICON_Y;

            hal_display_draw_rgb_bitmap(x, y, img, PRESSURE_ICONS_WIDTH, PRESSURE_ICONS_HEIGHT);
          }
        }
        break;
    }
  }

  switch(mode) {
    case PRESSURE_G_TURBO: {
      current = valueFields[F_PRESSURE_DESIRED];
      floatToDec(current, &hi, &lo);
      if(hi != lastHI_d || lo != lastLO_d) {
        lastHI_d = hi;
        lastLO_d = lo;

        x = getBaseX() + TURPO_PERCENT_TEXT_POS_X;
        y = getBaseY() + TURPO_PERCENT_TEXT_POS_Y;

        hal_display_set_default_font_with_pos_and_color(x, y, TEXT_COLOR);
        
        char txt[DISPLAY_TXT_SIZE];
        w = hal_display_prepare_text(txt, DISPLAY_TXT_SIZE, (const char*)F("req:%d.%dBAR"), hi, lo);

        hal_display_fill_rect(x, y, w + 10, 8, ICONS_BG_COLOR);
        hal_display_println_prepared_text(txt);
      }
    }
    break;
  }
}


