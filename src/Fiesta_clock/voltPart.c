/*
 * voltPart.c
 *
 *  Created on: 25 lut 2021
 *      Author: Marcin Kielesinski
 */

#include "voltPart.h"

#define V_TOO_LOW 5

static int lo = 0, hi = 0;
static bool clear = false, tooLow = false;
static int volt_delay = 0;
static double val = 0.0;

void voltMainFunction(void) {

	if(volt_delay-- <= 0) {
		volt_delay = VOLT_DELAY_LOOPS;

		val = ((double)getADCValue(6)) / 35.77732;

		doubleToDec(val, &hi, &lo);

		if(!tooLow && val <= ((double)V_TOO_LOW) && !clear) {
			clear = true;
			tooLow = true;
		}

		if(tooLow && val > ((double)V_TOO_LOW) && !clear) {
			clear = true;
			tooLow = false;
		}

		if(clear) {
			lcd_clrscr();
			clear = false;
		}
	}

	unsigned char x, y = 2;

	memset(s, 0, BUF_L);

	if(val < ((double)V_TOO_LOW)) {
		x = 2;
		snprintf(s, BUF_L, "za niskie! <= %dV", V_TOO_LOW);
		lcd_charMode(NORMALSIZE);
	} else {
		x = 4;
		snprintf(s, BUF_L, "%d.%dV ", hi, lo);
		lcd_charMode(DOUBLESIZE);
	}
	lcd_gotoxy(x, y);
	lcd_puts(s);

	lcd_charMode(NORMALSIZE);
	lcd_gotoxy(2, 0);
	lcd_puts("Zasilanie:");
}
