
//
//  main.h
//
//  Created by Marcin Kielesinski on 13/02/2021.
//

#ifndef main_h
#define main_h

#define BUF_L (128 + 1)

#define MODE_CLOCK 0
#define MODE_TEMP 1
#define MODE_VOLT 2

#include "utils.h"
#include "lcd.h"

#include "twi_i2c.h"
#include "PCF8563.h"
#include "clockPart.h"
#include "ds18b20.h"
#include "tempPart.h"
#include "adc.h"
#include "voltPart.h"

bool ignition(void);
bool setButtonPressed(void);
bool setButton(void);
bool setHour(void);
bool setMinute(void);
bool anyButton(void);

void redLED(bool state);
void orangeLED(bool state);
void blueLED(bool state);

#endif
