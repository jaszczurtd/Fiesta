/*
 * RTC.h
 */

#ifndef RTC_H_
#define RTC_H_

#include "utils.h"
#include <JaszczurHAL.h>

#define PCF8563_READ_ADDR 0xA3
#define PCF8563_WRITE_ADDR 0xA2

#define RTC_ALARM_FLAG (1 << 3)
#define RTC_TIMER_FLAG (1 << 2)
#define RTC_ALARM_INTERRUPT_ENABLE (1 << 1)
#define RTC_TIMER_INTERRUPT_ENABLE (1 << 0)

#define RTC_CLKOUT_32768HZ 0b10000000
#define RTC_CLKOUT_1024HZ 0b10000001
#define RTC_CLKOUT_32HZ 0b10000010
#define RTC_CLKOUT_1HZ 0b10000011
#define RTC_CLKOUT_DISABLED 0b00000000

#define RTC_TIMER_4096HZ 0b10000000
#define RTC_TIMER_64HZ 0b10000001
#define RTC_TIMER_1HZ 0b10000010
#define RTC_TIMER_1_60HZ 0b10000011
#define RTC_TIMER_DISABLED 0b00000011

#define RTC_DISABLE_ALARM 80

typedef struct {
  unsigned char minute;
  unsigned char hour;
  unsigned char day;
  unsigned char weekday;
} RTC_Alarm;

typedef struct {
  unsigned char second;
  unsigned char minute;
  unsigned char hour;
  unsigned char day;
  unsigned char weekday;
  unsigned char month;
  int year;
} RTC_DateTime;

void rtc_Write(unsigned char addr, unsigned char *data, unsigned char count);
void rtc_Read(unsigned char addr, unsigned char *data, unsigned char count);

void rtc_Init(unsigned char mode);
unsigned char rtc_GetAndClearFlags(void);

void rtc_SetClockOut(unsigned char mode);

void rtc_SetTimer(unsigned char mode, unsigned char count);
unsigned char rtc_GetTimer(void);

unsigned char rtc_SetAlarm(RTC_Alarm *alarm);
unsigned char rtc_GetAlarm(RTC_Alarm *alarm);

unsigned char rtc_GetDateTime(RTC_DateTime *dateTime);
/** Write UTC seconds; the HAL derives the weekday (0 = Sunday).
 * @return HAL_OK, HAL_EINVAL outside the RTC range, or HAL_EIO. */
hal_status_t rtc_WriteEpoch(uint32_t epoch);
/** Read the registers with the integrity bit.
 * @return HAL_OK, HAL_EINVAL for NULL, HAL_EIO for a bus error, or HAL_EPROTO
 *         for contents that are no valid date. */
hal_status_t rtc_ReadDateTime(hal_rtc_datetime_t *dateTime);

#endif /* RTC_H_ */
