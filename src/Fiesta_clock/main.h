
#ifndef MAIN_H_
#define MAIN_H_

#define BUF_L (128 + 1)

#define MODE_CLOCK 0
#define MODE_TEMP 1
#define MODE_VOLT 2

#include <tools_c.h>

#include "config.h"
#include "hardwareConfig.h"

#include "lcd.h"
#include "utils.h"

#include "RTC.h"
#include "adc.h"
#include "can.h"
#include "clockPart.h"
#include "ds18b20.h"
#include "tempPart.h"
#include "twi_i2c.h"
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

void initialization(void);
void looper(void);

#endif /* MAIN_H_ */
