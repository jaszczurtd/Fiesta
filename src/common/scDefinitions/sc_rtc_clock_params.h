#pragma once

/* SC parameter identifiers shared by RTC_Clock and the host. The date and
 * time are CET/CEST; the firmware converts them to the UTC its RTC keeps.
 * rtc_integrity is read-only: 1 only while the RTC holds trusted UTC. */
#define SC_RTC_YEAR_ID "rtc_year"
#define SC_RTC_MONTH_ID "rtc_month"
#define SC_RTC_DAY_ID "rtc_day"
#define SC_RTC_HOUR_ID "rtc_hour"
#define SC_RTC_MINUTE_ID "rtc_minute"
#define SC_RTC_SECOND_ID "rtc_second"
#define SC_RTC_INTEGRITY_ID "rtc_integrity"

/* X(identifier) for the writable date and time, in the order the host stages
 * them before a single commit. */
#define SC_RTC_DATETIME_PARAMS(X)                                              \
  X(SC_RTC_YEAR_ID)                                                            \
  X(SC_RTC_MONTH_ID)                                                           \
  X(SC_RTC_DAY_ID)                                                             \
  X(SC_RTC_HOUR_ID)                                                            \
  X(SC_RTC_MINUTE_ID)                                                          \
  X(SC_RTC_SECOND_ID)
