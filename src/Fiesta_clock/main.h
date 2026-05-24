
#ifndef MAIN_H_
#define MAIN_H_

#define BUF_L (128 + 1)

#define MODE_CLOCK 0
#define MODE_TEMP 1
#define MODE_VOLT 2

#include "config.h"
#include "hardwareConfig.h"

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

void setup_c(void);
void loop_c(void);

#endif /* MAIN_H_ */
