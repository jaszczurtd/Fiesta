//
// tempPart.c
//  Index
//
//  Created by Marcin Kielesinski on 25/02/2021.
//

#include "tempPart.h"

static int temp_in_hi = 0;
static int temp_in_lo = 0;
static int temp_out_hi = 0;
static int temp_out_lo = 0;
static double temp_out = 0.0;
static int temp_in_delay = 0;
static int temp_out_delay = 0;

void check_temp_for_leds(void) {
  if (temp_out < 4.1 && temp_out > 0.1) {
    orangeLED(true);
    redLED(false);
  } else if (temp_out < 0.1) {
    redLED(true);
    orangeLED(false);
  } else {
    orangeLED(false);
    redLED(false);
  }
}

void init_delay(void) {
  temp_in_delay = CFG_TEMP_DELAY_LOOPS;
  temp_out_delay = CFG_TEMP_DELAY_LOOPS * 2;
}

void temp_initial_read(void) {
  ds18b20_setPin(JH_HW_NODE_TEMP_INSIDE_PIN_DATA);
  ds18b20_gettemp(&temp_in_hi, &temp_in_lo, NULL);

  ds18b20_setPin(JH_HW_NODE_TEMP_OUTSIDE_PIN_DATA);
  ds18b20_gettemp(&temp_out_hi, &temp_out_lo, &temp_out);

  if (ignition()) {
    check_temp_for_leds();
  }
  init_delay();
}

void temp_read_display(void) {

  if (anyButton()) {
    init_delay();
  }

  if (temp_in_delay-- <= 0) {
    temp_in_delay = CFG_TEMP_DELAY_LOOPS;
    ds18b20_setPin(JH_HW_NODE_TEMP_INSIDE_PIN_DATA);
    ds18b20_gettemp(&temp_in_hi, &temp_in_lo, NULL);
  }

  if (temp_out_delay-- <= 0) {
    temp_out_delay = CFG_TEMP_DELAY_LOOPS;
    ds18b20_setPin(JH_HW_NODE_TEMP_OUTSIDE_PIN_DATA);
    ds18b20_gettemp(&temp_out_hi, &temp_out_lo, &temp_out);
    check_temp_for_leds();
  }

  unsigned char x, y = 2;

  memset(s, 0, BUF_L);
  snprintf(s, BUF_L, "%d.%d ", temp_in_hi, temp_in_lo);

  if (temp_in_hi < 0) {
    x = 0;
  } else {
    x = 1;
  }
  (void)clock_display_text(x, y, 2, s);

  memset(s, 0, BUF_L);
  snprintf(s, BUF_L, "%d.%d ", temp_out_hi, temp_out_lo);
  if (temp_in_hi < 0) {
    x = 11;
  } else {
    x = 12;
  }
  (void)clock_display_text(x, y, 2, s);

  (void)clock_display_text(2, 0, 1, "wewn.");
  (void)clock_display_text(13, 0, 1, "zewn.");
}
