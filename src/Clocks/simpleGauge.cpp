
#include "simpleGauge.h"
#include <utils/tools_common_defs.h>

SimpleGauge engineLoad_g = SimpleGauge(SIMPLE_G_ENGINE_LOAD);
SimpleGauge intake_g = SimpleGauge(SIMPLE_G_INTAKE);
SimpleGauge rpm_g = SimpleGauge(SIMPLE_G_RPM);
SimpleGauge gps_g = SimpleGauge(SIMPLE_G_GPS);
SimpleGauge egt_g = SimpleGauge(SIMPLE_G_EGT);
SimpleGauge volts_g = SimpleGauge(SIMPLE_G_VOLTS);
SimpleGauge ecu_g = SimpleGauge(SIMPLE_G_ECU);
SimpleGauge dpf_g = SimpleGauge(SIMPLE_G_DPF);

void redrawSimpleGauges(void) {
  engineLoad_g.redraw();
  intake_g.redraw();
  rpm_g.redraw();
  gps_g.redraw();
  egt_g.redraw();
  volts_g.redraw();
  ecu_g.redraw();
}

void showEngineLoadGauge(void) { engineLoad_g.showSimpleGauge(); }

void showGPSGauge(void) { gps_g.showSimpleGauge(); }

void showSimpleGauges(void) {
  intake_g.showSimpleGauge();
  rpm_g.showSimpleGauge();
  volts_g.showSimpleGauge();
}

void showECUConnectionGauge(void) {
  ecu_g.showSimpleGauge();
  dpf_g.showSimpleGauge();
}

void showEGTGauge(void) { egt_g.showSimpleGauge(); }

// The channel inside the DPF takes turns with the one before it only while a
// connected DPF module reports a regeneration; otherwise the gauge shows EGT.
void changeEGT(void) {
  if (isDPFConnected() && isDPFRegenerating()) {
    egt_g.switchCurrentEGTMode();
  } else {
    egt_g.selectEGTMode(false);
  }
}

SimpleGauge::SimpleGauge(int mode) {
  this->mode = mode;
  drawOnce = true;
  currentIsDPF = false;
  lastV1 = lastV2 = C_INIT_VAL;

  switch (mode) {
  case SIMPLE_G_ECU:
    lastShowedVal = isEcuConnected();
    break;

  case SIMPLE_G_DPF:
    lastShowedVal = isDPFConnected();
    break;

  default:
    lastShowedVal = C_INIT_VAL;
    break;
  }
}

int SimpleGauge::drawTextForMiddleIcons(int x, int y, int offset, int color,
                                        int mode, const char *format, ...) {

  int w1 = 0, kmoffset = 0;
  const char *km = "km/h";
  if (mode == MODE_M_KILOMETERS) {
    hal_display_set_default_font();
    w1 = hal_display_text_width(km);
    kmoffset = 5;
  }
  hal_display_set_serif9pt_with_color((uint16_t)color);

  va_list valist;
  va_start(valist, format);
  int w = hal_display_prepare_text_v(displayTxt, sizeof(displayTxt), format,
                                     valist);
  va_end(valist);

  int x1 = x + ((SMALL_ICONS_WIDTH - w - w1 - kmoffset) / 2) - kmoffset;
  int y1 = y + 59;

  hal_display_fill_rect(x + offset, y1 - 14, SMALL_ICONS_WIDTH - (offset * 2),
                        16, ICONS_BG_COLOR);
  hal_display_print_at(x1, y1, displayTxt);

  switch (mode) {
  default:
  case MODE_M_NORMAL:
    break;
  case MODE_M_TEMP:
    hal_display_draw_circle(x1 + w + 6, y1 - 10, 3, color);
    break;
  case MODE_M_KILOMETERS:
    hal_display_set_default_font();
    hal_display_print_at(x1 + w + kmoffset, y1 - 6, km);
    return w;
  }

  hal_display_set_default_font();
  return w;
}

int SimpleGauge::getBaseX(void) {
  switch (mode) {
  case SIMPLE_G_GPS:
    return (SMALL_ICONS_WIDTH * 0) + 8;
  case SIMPLE_G_RPM:
    return (SMALL_ICONS_WIDTH * 1) + 1;
  case SIMPLE_G_EGT:
    return (SMALL_ICONS_WIDTH * 2);
  case SIMPLE_G_INTAKE:
    return (SMALL_ICONS_WIDTH * 3);
  case SIMPLE_G_ENGINE_LOAD:
    return (SMALL_ICONS_WIDTH * 4);
  case SIMPLE_G_VOLTS:
    return 223;
  case SIMPLE_G_ECU:
    return SCREEN_W - (3 * ECU_CONNECTION_RADIUS);
  case SIMPLE_G_DPF:
    return SCREEN_W - (6 * ECU_CONNECTION_RADIUS);
  }
  return -1;
}

int SimpleGauge::getBaseY(void) {
  switch (mode) {
  case SIMPLE_G_GPS:
  case SIMPLE_G_RPM:
  case SIMPLE_G_EGT:
  case SIMPLE_G_INTAKE:
  case SIMPLE_G_ENGINE_LOAD:
    return BIG_ICONS_HEIGHT + (BIG_ICONS_OFFSET * 2);
  case SIMPLE_G_VOLTS:
    return 185;
  case SIMPLE_G_DPF:
  case SIMPLE_G_ECU:
    return SCREEN_H - (3 * ECU_CONNECTION_RADIUS);
  }
  return -1;
}

void SimpleGauge::redraw(void) { drawOnce = true; }

void SimpleGauge::switchCurrentEGTMode(void) {
  currentIsDPF = !currentIsDPF;
  redraw();
}

void SimpleGauge::selectEGTMode(bool dpf) {
  if (currentIsDPF != dpf) {
    currentIsDPF = dpf;
    redraw();
  }
}

void SimpleGauge::showSimpleGauge(void) {

  unsigned short *tempImg = NULL;
  bool draw = false;
  int x, y;

  float volts = valueFields[F_VOLTS];
  int v1, v2;
  int color = TEXT_COLOR;

  switch (mode) {

  case SIMPLE_G_VOLTS:
    fiesta_can_split_decimal_tenths(volts, &v1, &v2);
    if (v1 != lastV1 || v2 != lastV2) {
      lastV1 = v1;
      lastV2 = v2;

      if (volts < VOLTS_MIN_VAL || volts > VOLTS_MAX_VAL) {
        tempImg = (unsigned short *)batteryNotOKIcon;
      } else {
        tempImg = (unsigned short *)batteryOKIcon;
      }

      drawOnce = true;
      draw = true;
    }
    break;

  case SIMPLE_G_ECU:
    if (!isEcuConnected()) {
      draw = true;
    }
    break;

  case SIMPLE_G_DPF:
    if (DPF_MODULE_INSTALLED && !isDPFConnected()) {
      draw = true;
    }
    break;
  }

  int w = SMALL_ICONS_WIDTH;
  int h = SMALL_ICONS_HEIGHT;
  if (drawOnce) {
    switch (mode) {
    case SIMPLE_G_ENGINE_LOAD:
      tempImg = (unsigned short *)pump;
      break;
    case SIMPLE_G_INTAKE:
      tempImg = (unsigned short *)ic;
      break;
    case SIMPLE_G_RPM:
      tempImg = (unsigned short *)rpm;
      break;
    case SIMPLE_G_GPS:
      tempImg = (unsigned short *)gpsIcon;
      break;
    case SIMPLE_G_EGT:
      tempImg = (unsigned short *)egt;
      if (currentIsDPF) {
        tempImg = (unsigned short *)dpf;
      }
      break;
    case SIMPLE_G_VOLTS:
      w = BATTERY_WIDTH;
      h = BATTERY_HEIGHT;
      break;
    }

    if (tempImg != NULL) {
      hal_display_draw_image(getBaseX(), getBaseY(), w, h, ICONS_BG_COLOR,
                             tempImg);
    }
    drawOnce = false;
    draw = true;
  }

  int currentVal = 0;

  switch (mode) {
  case SIMPLE_G_ENGINE_LOAD:
    currentVal = getThrottlePercentage();
    break;
  case SIMPLE_G_INTAKE:
    currentVal = int(valueFields[F_INTAKE_TEMP]);
    break;
  case SIMPLE_G_RPM:
    currentVal = ((getEngineRPM() / 10) * 10);
    break;
  case SIMPLE_G_GPS:
    // Without communication there is no speed to show, as without a
    // thermocouple.
    currentVal = isGPSLinked() ? getGPSSpeed() : GPS_NOT_LINKED;
    break;
  case SIMPLE_G_ECU:
    currentVal = isEcuConnected();
    break;
  case SIMPLE_G_DPF:
    currentVal = isDPFConnected();
    break;
  }

  const char *format = NULL;
  int txtMode = MODE_M_NORMAL;
  int offset = 5;

  if (lastShowedVal != currentVal) {
    switch (mode) {
    case SIMPLE_G_ENGINE_LOAD:
      format = "%d%%";
      break;

    case SIMPLE_G_RPM:
      format = "%d";
      break;

    case SIMPLE_G_GPS:
      offset = 1;
      if (currentVal == GPS_NOT_LINKED) {
        format = "N/A";
      } else {
        format = "%d";
        txtMode = MODE_M_KILOMETERS;
      }
      break;

    case SIMPLE_G_INTAKE:
      bool error = currentVal < TEMP_LOWEST || currentVal > TEMP_HIGHEST;

      if (error) {
        color = HAL_COLOR(RED);
        format = (const char *)err;
      } else {
        format = "%d";
      }

      txtMode = (error) ? MODE_M_NORMAL : MODE_M_TEMP;
      break;
    }

    switch (mode) {
    case SIMPLE_G_ENGINE_LOAD:
    case SIMPLE_G_RPM:
    case SIMPLE_G_GPS:
    case SIMPLE_G_INTAKE:
      drawTextForMiddleIcons(getBaseX(), getBaseY(), offset, color, txtMode,
                             format, currentVal);
      lastShowedVal = currentVal;
      break;

    case SIMPLE_G_DPF:
    case SIMPLE_G_ECU:
      draw = true;
      drawOnce = true;
      lastShowedVal = currentVal;
      break;
    }
  }

  switch (mode) {
  case SIMPLE_G_GPS: {

    // Green with a fix; red blinking without one, twice as fast while the
    // receiver does not communicate.
    if (isGPSAvailable()) {
      color = HAL_COLOR(GREEN);
    } else if (!isGPSLinked()) {
      color = (seriousAlertSwitch()) ? HAL_COLOR(RED) : ICONS_BG_COLOR;
    } else {
      color = (alertSwitch()) ? HAL_COLOR(RED) : ICONS_BG_COLOR;
    }

    int posOffset = 10;
    int radius = 4;

    x = getBaseX() + SMALL_ICONS_WIDTH - posOffset - radius;
    y = getBaseY() + posOffset - 1;

    hal_display_fill_circle(x, y, radius, color);

  } break;

  case SIMPLE_G_EGT: {
    // A channel without a measurement shows N/A, never a pretended value.
    const bool measured = isEgtValid(currentIsDPF ? EGT_IN_DPF : EGT_PRE_DPF);
    currentVal = EGT_NOT_MEASURED;
    if (measured) {
      currentVal = (int)valueFields[currentIsDPF ? F_DPF_TEMP : F_EGT];
      if (currentVal < TEMP_EGT_MIN) {
        currentVal = TEMP_EGT_MIN - 1;
      }
    }

    if (lastShowedVal != currentVal) {
      lastShowedVal = currentVal;
      draw = true;
    }

    bool overheat = false;
    if (measured && currentVal > TEMP_EGT_OK_HI) {
      overheat = true;
    }

    if (overheat) {
      draw = true;
      color = (seriousAlertSwitch()) ? HAL_COLOR(RED) : TEXT_COLOR;
    }

    if (draw) {
      if (!measured) {
        format = "N/A";
      } else if (currentVal < TEMP_EGT_MIN) {
        format = "COLD";
      } else if (currentVal > TEMP_EGT_MAX) {
        format = ((char *)err);
      } else {
        format = "%d";
        if (currentIsDPF) {
          if (isDPFRegenerating()) {
            format = "R/%d";
          }
        }
      }

      bool isTemp =
          measured && (currentVal > TEMP_EGT_MIN && currentVal < TEMP_EGT_MAX);
      txtMode = MODE_M_NORMAL;
      if (isTemp) {
        txtMode = MODE_M_TEMP;
      }

      drawTextForMiddleIcons(getBaseX(), getBaseY(), 2, color, txtMode, format,
                             currentVal);
    }
  } break;

  case SIMPLE_G_VOLTS: {
    if (draw) {
      x = getBaseX() + BATTERY_WIDTH + 2;
      y = getBaseY() + 26;

      hal_display_fill_rect(x, y - 14, 45, 16, ICONS_BG_COLOR);

      color = TEXT_COLOR;
      if (volts < VOLTS_MIN_VAL || volts > VOLTS_MAX_VAL) {
        color = HAL_COLOR(RED);
      }

      char txt[DISPLAY_TXT_SIZE];
      hal_display_prepare_text(txt, DISPLAY_TXT_SIZE, "%d.%dv", v1, v2);
      hal_display_set_sans_bold_with_pos_and_color(x, y, (uint16_t)color);
      hal_display_println_prepared_text(txt);
    }
  } break;

  case SIMPLE_G_DPF:
  case SIMPLE_G_ECU: {
    x = getBaseX();
    y = getBaseY();

    if (draw) {

      switch (mode) {
      case SIMPLE_G_ECU:
      default:
        color = (seriousAlertSwitch()) ? HAL_COLOR(RED) : ICONS_BG_COLOR;
        break;

      case SIMPLE_G_DPF:
        // No alarm for a module that is not installed yet.
        color =
            (DPF_MODULE_INSTALLED && !isDPFConnected() && !seriousAlertSwitch())
                ? HAL_COLOR(PURPLE)
                : ICONS_BG_COLOR;
        break;
      }

      hal_display_fill_circle(x, y, ECU_CONNECTION_RADIUS, color);
      draw = false;
    }

    if (drawOnce) {
      hal_display_fill_rect(
          x - ECU_CONNECTION_RADIUS, y - ECU_CONNECTION_RADIUS,
          ECU_CONNECTION_RADIUS * 3, ECU_CONNECTION_RADIUS * 3, ICONS_BG_COLOR);
      drawOnce = false;
    }
  } break;
  }
}
