#include "RTC.h"

#include <string.h>

#include "hardwareConfig.h"

static hal_rtc_t s_rtc = NULL;

static uint8_t rtc_i2c_address(void) {
#if defined(RTC_ds3231)
  return HAL_RTC_DS3231_DEFAULT_I2C_ADDR;
#else
  return JH_HW_NODE_RTC_ADDRESS;
#endif
}

static bool rtc_ensure_rtc(void) {
  if (s_rtc) {
    return true;
  }

  hal_rtc_config_t cfg = {0};
#if defined(RTC_ds3231)
  cfg.chip = HAL_RTC_CHIP_DS3231;
#else
  cfg.chip = HAL_RTC_CHIP_PCF8563;
#endif
  cfg.bus.i2c.sda_pin = JH_HW_BUS_I2C_PIN_SDA;
  cfg.bus.i2c.scl_pin = JH_HW_BUS_I2C_PIN_SCL;
  cfg.bus.i2c.clock_hz = JH_HW_BUS_I2C_FREQUENCY_HZ;
  cfg.bus.i2c.i2c_bus = JH_HW_BUS_I2C_INDEX;
  cfg.bus.i2c.i2c_addr = rtc_i2c_address();

  s_rtc = hal_rtc_init(&cfg);
  return s_rtc != NULL;
}

static hal_rtc_clkout_mode_t rtc_clkout_mode(unsigned char mode) {
  switch (mode & 0x83u) {
  case RTC_CLKOUT_1HZ:
    return HAL_RTC_CLKOUT_1_HZ;
  case RTC_CLKOUT_32HZ:
    return HAL_RTC_CLKOUT_32_HZ;
  case RTC_CLKOUT_1024HZ:
    return HAL_RTC_CLKOUT_1024_HZ;
  case RTC_CLKOUT_32768HZ:
    return HAL_RTC_CLKOUT_32768_HZ;
  default:
    return HAL_RTC_CLKOUT_DISABLED;
  }
}

static hal_rtc_timer_clock_t rtc_timer_mode(unsigned char mode) {
  switch (mode & 0x83u) {
  case RTC_TIMER_1_60HZ:
    return HAL_RTC_TIMER_1_60_HZ;
  case RTC_TIMER_1HZ:
    return HAL_RTC_TIMER_1_HZ;
  case RTC_TIMER_64HZ:
    return HAL_RTC_TIMER_64_HZ;
  case RTC_TIMER_4096HZ:
    return HAL_RTC_TIMER_4096_HZ;
  default:
    return HAL_RTC_TIMER_DISABLED;
  }
}

void rtc_Write(unsigned char addr, unsigned char *data, unsigned char count) {
  if (!data || count == 0u) {
    return;
  }

  hal_i2c_begin_transmission_bus(JH_HW_BUS_I2C_INDEX, rtc_i2c_address());
  (void)hal_i2c_write_bus(JH_HW_BUS_I2C_INDEX, addr);
  for (unsigned char i = 0; i < count; ++i) {
    (void)hal_i2c_write_bus(JH_HW_BUS_I2C_INDEX, data[i]);
  }
  (void)hal_i2c_end_transmission_bus(JH_HW_BUS_I2C_INDEX);
}

void rtc_Read(unsigned char addr, unsigned char *data, unsigned char count) {
  if (!data || count == 0u) {
    return;
  }

  memset(data, 0, count);

  hal_i2c_begin_transmission_bus(JH_HW_BUS_I2C_INDEX, rtc_i2c_address());
  (void)hal_i2c_write_bus(JH_HW_BUS_I2C_INDEX, addr);
  if (hal_i2c_end_transmission_bus(JH_HW_BUS_I2C_INDEX) != 0u) {
    return;
  }

  if (hal_i2c_request_from_bus(JH_HW_BUS_I2C_INDEX, rtc_i2c_address(), count) !=
      count) {
    return;
  }

  for (unsigned char i = 0; i < count; ++i) {
    const int raw = hal_i2c_read_bus(JH_HW_BUS_I2C_INDEX);
    if (raw < 0) {
      return;
    }
    data[i] = (unsigned char)raw;
  }
}

void rtc_Init(unsigned char mode) {
  if (!rtc_ensure_rtc()) {
    return;
  }

  uint8_t irq_mask = 0;
  if ((mode & RTC_ALARM_INTERRUPT_ENABLE) != 0u) {
    irq_mask = (uint8_t)(irq_mask | HAL_RTC_IRQ_ALARM);
  }
#if !defined(RTC_ds3231)
  if ((mode & RTC_TIMER_INTERRUPT_ENABLE) != 0u) {
    irq_mask = (uint8_t)(irq_mask | HAL_RTC_IRQ_TIMER);
  }
#endif

  (void)hal_rtc_set_interrupt_enable(s_rtc, irq_mask);
}

unsigned char rtc_GetAndClearFlags(void) {
  if (!rtc_ensure_rtc()) {
    return 0u;
  }

  uint8_t flags = 0;
  if (!hal_rtc_get_and_clear_flags(s_rtc, &flags)) {
    return 0u;
  }

  uint8_t out = 0;
  if ((flags & HAL_RTC_FLAG_ALARM) != 0u) {
    out = (uint8_t)(out | RTC_ALARM_FLAG);
  }
  if ((flags & HAL_RTC_FLAG_TIMER) != 0u) {
    out = (uint8_t)(out | RTC_TIMER_FLAG);
  }
  return out;
}

void rtc_SetClockOut(unsigned char mode) {
  if (!rtc_ensure_rtc()) {
    return;
  }
  (void)hal_rtc_set_clkout_mode(s_rtc, rtc_clkout_mode(mode));
}

void rtc_SetTimer(unsigned char mode, unsigned char count) {
  if (!rtc_ensure_rtc()) {
    return;
  }
  (void)hal_rtc_set_timer(s_rtc, rtc_timer_mode(mode), count);
}

unsigned char rtc_GetTimer(void) {
  if (!rtc_ensure_rtc()) {
    return 0u;
  }

  hal_rtc_timer_clock_t timer_clock = HAL_RTC_TIMER_DISABLED;
  uint8_t count = 0;
  (void)hal_rtc_get_timer(s_rtc, &timer_clock, &count);
  (void)timer_clock;
  return count;
}

unsigned char rtc_SetAlarm(RTC_Alarm *alarm) {
  if (!alarm || !rtc_ensure_rtc()) {
    return 1u;
  }

  if ((alarm->minute >= 60u && alarm->minute != RTC_DISABLE_ALARM) ||
      (alarm->hour >= 24u && alarm->hour != RTC_DISABLE_ALARM) ||
      (alarm->day > 32u && alarm->day != RTC_DISABLE_ALARM) ||
      (alarm->weekday > 6u && alarm->weekday != RTC_DISABLE_ALARM)) {
    return 1u;
  }

  hal_rtc_alarm_t rtc_alarm = {0};
  rtc_alarm.minute_enabled = (alarm->minute != RTC_DISABLE_ALARM);
  rtc_alarm.hour_enabled = (alarm->hour != RTC_DISABLE_ALARM);
  rtc_alarm.day_enabled = (alarm->day != RTC_DISABLE_ALARM);
  rtc_alarm.weekday_enabled = (alarm->weekday != RTC_DISABLE_ALARM);

  rtc_alarm.minute = alarm->minute;
  rtc_alarm.hour = alarm->hour;
  rtc_alarm.day = alarm->day;
  rtc_alarm.weekday = alarm->weekday;

  return hal_rtc_set_alarm(s_rtc, &rtc_alarm) ? 0u : 1u;
}

unsigned char rtc_GetAlarm(RTC_Alarm *alarm) {
  if (!alarm || !rtc_ensure_rtc()) {
    return 1u;
  }

  hal_rtc_alarm_t rtc_alarm = {0};
  if (!hal_rtc_get_alarm(s_rtc, &rtc_alarm)) {
    return 1u;
  }

  alarm->minute =
      rtc_alarm.minute_enabled ? rtc_alarm.minute : RTC_DISABLE_ALARM;
  alarm->hour = rtc_alarm.hour_enabled ? rtc_alarm.hour : RTC_DISABLE_ALARM;
  alarm->day = rtc_alarm.day_enabled ? rtc_alarm.day : RTC_DISABLE_ALARM;
  alarm->weekday =
      rtc_alarm.weekday_enabled ? rtc_alarm.weekday : RTC_DISABLE_ALARM;

  return 0u;
}

hal_status_t rtc_WriteEpoch(uint32_t epoch) {
  if (!rtc_ensure_rtc()) {
    return HAL_EIO;
  }
  return hal_rtc_set_epoch_ex(s_rtc, epoch);
}

hal_status_t rtc_ReadDateTime(hal_rtc_datetime_t *dateTime) {
  if (dateTime == NULL) {
    return HAL_EINVAL;
  }
  if (!rtc_ensure_rtc()) {
    return HAL_EIO;
  }
  return hal_rtc_get_datetime_ex(s_rtc, dateTime);
}

unsigned char rtc_GetDateTime(RTC_DateTime *dateTime) {
  if (!dateTime || !rtc_ensure_rtc()) {
    return 1u;
  }

  hal_rtc_datetime_t dt = {0};
  if (!hal_rtc_get_datetime(s_rtc, &dt)) {
    return 1u;
  }

  dateTime->second = dt.second;
  dateTime->minute = dt.minute;
  dateTime->hour = dt.hour;
  dateTime->day = dt.day;
  dateTime->weekday = dt.weekday;
  dateTime->month = dt.month;
  dateTime->year = (int)dt.year;

  return dt.clock_integrity ? 0u : 1u;
}
