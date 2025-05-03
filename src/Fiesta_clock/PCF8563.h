/*
 * PCF8563.h
 *
 * Created: 2014-11-18 20:00:42
 *  Author: Jakub Pachciarek
 */


#ifndef PCF8563_H_
#define PCF8563_H_

#include "twi_i2c.h"
#include "utils.h"
#include "lcd.h"

#define PCF8563_READ_ADDR                0xA3
#define PCF8563_WRITE_ADDR                0xA2

#define PCF_ALARM_FLAG                    (1<<3)
#define PCF_TIMER_FLAG                    (1<<2)
#define PCF_ALARM_INTERRUPT_ENABLE        (1<<1)
#define PCF_TIMER_INTERRUPT_ENABLE        (1<<0)

#define PCF_CLKOUT_32768HZ                0b10000000
#define PCF_CLKOUT_1024HZ                0b10000001
#define PCF_CLKOUT_32HZ                    0b10000010
#define PCF_CLKOUT_1HZ                    0b10000011
#define PCF_CLKOUT_DISABLED                0b00000000

#define PCF_TIMER_4096HZ                0b10000000
#define PCF_TIMER_64HZ                    0b10000001
#define PCF_TIMER_1HZ                    0b10000010
#define PCF_TIMER_1_60HZ                0b10000011
#define PCF_TIMER_DISABLED                0b00000011

#define PCF_DISABLE_ALARM                80

#define PCF_MAX_YEAR 2100
#define PCF_MIN_YEAR 1900

typedef struct {
    unsigned char minute;
    unsigned char hour;
    unsigned char day;
    unsigned char weekday;
} PCF_Alarm;

typedef struct {
    unsigned char second;
    unsigned char minute;
    unsigned char hour;
    unsigned char day;
    unsigned char weekday;
    unsigned char month;
    int year;
} PCF_DateTime;


void PCF_Write(unsigned char addr, unsigned char *data, unsigned char count);
void PCF_Read(unsigned char addr, unsigned char *data, unsigned char count);

void PCF_Init(unsigned char mode);
unsigned char PCF_GetAndClearFlags(void);

void PCF_SetClockOut(unsigned char mode);

void PCF_SetTimer(unsigned char mode, unsigned char count);
unsigned char PCF_GetTimer(void);

unsigned char PCF_SetAlarm(PCF_Alarm *alarm);
unsigned char PCF_GetAlarm(PCF_Alarm *alarm);

unsigned char PCF_SetDateTime(PCF_DateTime *dateTime);
unsigned char PCF_GetDateTime(PCF_DateTime *dateTime);


#endif /* PCF8563_H_ */
