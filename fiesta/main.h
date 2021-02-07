/**

 */

#include "version.h" // Config file

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <inttypes.h>
#include <avr/wdt.h>

#include "utils.h"
#include "twi_i2c.h"
#include "i2cEeprom.h"
#include "UART.h"
#include "adc.h"
#include "pwm.h"
#include "pcf8574a.h"
#include "SSD1306.h"

extern int rc5Code, switchCode;
extern unsigned char lastLoudness;

extern unsigned char MEM[];
extern char s[];

#define MAIN_DELAY_TIME 7
//in ms
#define POWER_RES_COUNTER 100       //* MAIN_DELAY_TIME
#define SPEAKERS_COUNTER 35         //POWER_RES_COUNTER + (* MAIN_DELAY_TIME)
#define READ_COMMANDS_DELAY_TIME (MAIN_DELAY_TIME * 2)

#define POWER_OFF_DELAY 120
#define LOUDNESS_START_DELAY 200
#define LOUDNESS_END_DELAY 1100

#define POWER_LED_MAX_VALUE 160


#define DEFAULT_POWER_IS_ON false

#define VOL_CHANGE_TIME 55
#define VOL_BAR_HEIGHT 9
#define VOL_BAR_X 0
#define VOL_BAR_Y 20

#ifndef MAIN_H
#define MAIN_H

#endif

