/*
 * voltPart.c
 *
 *  Created on: 25 lut 2021
 *      Author: Marcin Kielesinski
 */

#include "voltPart.h"

static int lo = 0, hi = 0;
static bool clear = false, tooLow = false;
static int volt_delay = 0;
static double val = 0.0;

static double compute_voltage(unsigned char adc_channel) {
  const int raw = getADCValue(adc_channel);
  const double adc_max = (double)((1u << CFG_ADC_RESOLUTION_BITS) - 1u);
  const double adc_voltage = ((double)raw / adc_max) * CFG_ADC_VREF_VOLTS;
  const double divider_scale =
      (VOLT_DIVIDER_R_TOP_OHMS + VOLT_DIVIDER_R_BOTTOM_OHMS) /
      VOLT_DIVIDER_R_BOTTOM_OHMS;
  return adc_voltage * divider_scale;
}

void voltMainFunction(void) {

  if (volt_delay-- <= 0) {
    volt_delay = CFG_VOLT_DELAY_LOOPS;

    val = compute_voltage(6u);

    doubleToDec(val, &hi, &lo);

    if (!tooLow && val <= CFG_VOLT_TOO_LOW_V && !clear) {
      clear = true;
      tooLow = true;
    }

    if (tooLow && val > CFG_VOLT_TOO_LOW_V && !clear) {
      clear = true;
      tooLow = false;
    }

    if (clear) {
      (void)hal_display_fill_screen_ex(HAL_COLOR_BLACK);
      clear = false;
    }
  }

  unsigned char x, y = 2;
  uint8_t scale;

  memset(s, 0, BUF_L);

  if (val < CFG_VOLT_TOO_LOW_V) {
    x = 2;
    snprintf(s, BUF_L, "za niskie! <= %dV", CFG_VOLT_TOO_LOW_INT);
    scale = 1;
  } else {
    x = 4;
    snprintf(s, BUF_L, "%d.%dV ", hi, lo);
    scale = 2;
  }
  (void)clock_display_text(x, y, scale, s);

  (void)clock_display_text(2, 0, 1, "Zasilanie:");
}
