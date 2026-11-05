//
//  clockPart.c
//  Index
//
//  Created by Marcin Kielesiński on 27/01/2020.
//

#include "clockPart.h"

static RTC_DateTime rtcDateTime;
static bool clockSetMode = false;

static unsigned char activeItem = 0;
static bool itemVisible = true;
static int itemVisibilityCounter = 0;

static char day[4] = {0};
static char month[4] = {0};
static char year[5] = {0};
static char weekday[7] = {0};
static char hour[4] = {0};
static char minute[4] = {0};
static char second[4] = {0};

#define D_DAY 0
#define D_MONTH 1
#define D_YEAR 2
#define D_WEEKDAY 3
#define D_HOUR 4
#define D_MINUTE 5
#define D_SECOND 6

#define CLOCK_SETTABLE_ITEMS 7

static const char *items[CLOCK_SETTABLE_ITEMS] = {day,  month,  year,  weekday,
                                                  hour, minute, second};

void printCurrentItemsState(void) {
  snprintf(day, sizeof(day), "%02d", rtcDateTime.day);
  snprintf(month, sizeof(month), "%02d", rtcDateTime.month);
  snprintf(year, sizeof(year), "%d", rtcDateTime.year);
  snprintf(hour, sizeof(hour), "%02d", rtcDateTime.hour);
  snprintf(minute, sizeof(minute), "%02d", rtcDateTime.minute);
  snprintf(second, sizeof(second), "%02d", rtcDateTime.second);

  switch (rtcDateTime.weekday) {
  case 0:
    strcpy(weekday, "pon.");
    break;
  case 1:
    strcpy(weekday, "wt.");
    break;
  case 2:
    strcpy(weekday, "sr.");
    weekday[0] = 0x7f;
    break;
  case 3:
    strcpy(weekday, "czw.");
    break;
  case 4:
    strcpy(weekday, "pt.");
    break;
  case 5:
    strcpy(weekday, "sob.");
    break;
  case 6:
    strcpy(weekday, "niedz.");
    break;
  }
}

const char *getItem(unsigned char number) {
  if (activeItem == number) {

    static char *empty = NULL;

    switch (number) {
    case D_DAY:
    case D_MONTH:
    case D_HOUR:
    case D_MINUTE:
    case D_SECOND:
      empty = "  ";
      break;
    case D_YEAR:
      empty = "    ";
      break;
    case D_WEEKDAY:
      empty = "      ";
      break;
    }

    return itemVisible ? items[number] : empty;
  }
  return items[number];
}

void setItemVisible(void) {
  itemVisible = true;
  itemVisibilityCounter = 0;
}

void pause(void) { hal_delay_ms(CFG_CLOCK_OPERATION_DELAY_MS); }

void setItem(bool increase) {
  switch (activeItem) {
  case D_DAY:
    if (increase) {
      if (rtcDateTime.day < 31) {
        rtcDateTime.day++;
      } else {
        rtcDateTime.day = 1;
      }
    } else {
      if (rtcDateTime.day > 1) {
        rtcDateTime.day--;
      } else {
        rtcDateTime.day = 31;
      }
    }
    break;

  case D_MONTH:
    if (increase) {
      if (rtcDateTime.month < 12) {
        rtcDateTime.month++;
      } else {
        rtcDateTime.month = 1;
      }
    } else {
      if (rtcDateTime.month > 1) {
        rtcDateTime.month--;
      } else {
        rtcDateTime.month = 12;
      }
    }
    break;

  case D_YEAR:
    if (increase) {
      if (rtcDateTime.year < CFG_RTC_MAX_YEAR) {
        rtcDateTime.year++;
      } else {
        rtcDateTime.year = CFG_RTC_MIN_YEAR;
      }
    } else {
      if (rtcDateTime.year > CFG_RTC_MIN_YEAR) {
        rtcDateTime.year--;
      } else {
        rtcDateTime.year = CFG_RTC_MAX_YEAR;
      }
    }
    break;

  case D_HOUR:
    if (increase) {
      if (rtcDateTime.hour < 23) {
        rtcDateTime.hour++;
      } else {
        rtcDateTime.hour = 0;
      }
    } else {
      if (rtcDateTime.hour > 0) {
        rtcDateTime.hour--;
      } else {
        rtcDateTime.hour = 23;
      }
    }
    break;

  case D_MINUTE:
    if (increase) {
      if (rtcDateTime.minute < 59) {
        rtcDateTime.minute++;
      } else {
        rtcDateTime.minute = 0;
      }
    } else {
      if (rtcDateTime.minute > 0) {
        rtcDateTime.minute--;
      } else {
        rtcDateTime.minute = 59;
      }
    }
    break;

  case D_SECOND:
    if (increase) {
      if (rtcDateTime.second < 59) {
        rtcDateTime.second++;
      } else {
        rtcDateTime.second = 0;
      }
    } else {
      if (rtcDateTime.second > 0) {
        rtcDateTime.second--;
      } else {
        rtcDateTime.second = 59;
      }
    }
    break;
  }
  /* The weekday follows the date; it is shown but not set. */
  clockTimeUpdateWeekday(&rtcDateTime);
  setItemVisible();
  pause();
}

void setClockSetMode(bool enabled) {
  clockSetMode = enabled;
  if (clockSetMode) {
    activeItem = 0;
    setItemVisible();
  }
}

bool isClockSetMode(void) { return clockSetMode; }

void getTime(void) { (void)clockTimeReadLocal(&rtcDateTime, NULL); }

static void storeTime(void) {
  const hal_status_t status = clockTimeWriteLocal(&rtcDateTime);
  if (status != HAL_OK) {
    derr("RTC time not stored as UTC: %s", hal_status_to_string(status));
  }
}

static int clockSetModeDelayLoops = 0;
static bool clockSetModeOrdered = false;
static int hourDelay = 0, minuteDelay = 0;

void manageSetMode(void) {
  if (setButton()) {

    if (CFG_CLOCK_SET_MODE_DELAY_LOOPS < clockSetModeDelayLoops++) {
      clockSetModeOrdered = true;
      clockSetModeDelayLoops = 0;
      activeItem = 0;

      (void)hal_display_fill_screen_ex(HAL_COLOR_BLACK);
      (void)clock_display_text(6, 2, 2, "PUSC");
      (void)hal_display_flush_ex();

      hourDelay = minuteDelay = 0;

      if (setButtonPressed()) {
      }
    }
  } else {
    clockSetModeDelayLoops = 0;
    clockSetModeOrdered = false;
  }
}

void clockMainFunction(void) {

  if (!clockSetMode) {

    getTime();

    manageSetMode();

    if (clockSetModeOrdered) {
      clockSetModeOrdered = false;
      pause();
      setClockSetMode(true);
      setItemVisible();
      return;
    }

    if (ignition()) {
      if (setHour()) {

        while (setHour()) {
          if (hourDelay > 5) {
            break;
          }
          hourDelay++;
          hal_delay_ms(CFG_CLOCK_SET_STEP_DELAY_MS);
        }

        activeItem = D_HOUR;
        setItem(true);
        storeTime();
        hal_delay_ms(CFG_CLOCK_SET_STEP_DELAY_MS);
      } else {
        hourDelay = 0;
      }

      if (setMinute()) {

        while (setMinute()) {
          if (minuteDelay > 5) {
            break;
          }
          minuteDelay++;
          hal_delay_ms(CFG_CLOCK_SET_STEP_DELAY_MS);
        }

        activeItem = D_MINUTE;
        setItem(true);
        storeTime();
        hal_delay_ms(CFG_CLOCK_SET_STEP_DELAY_MS);
      } else {
        minuteDelay = 0;
      }
    }

  } else {

    if (setButtonPressed()) {
      pause();

      if (++activeItem == D_WEEKDAY) {
        ++activeItem;
      }
      if (activeItem > CLOCK_SETTABLE_ITEMS - 1) {
        setClockSetMode(false);

        storeTime();
        return;
      }
    } else if (setHour()) {
      setItem(true);
    } else if (setMinute()) {
      setItem(false);
    }

    if (itemVisibilityCounter++ > CFG_CLOCK_ITEM_VISIBILITY_CYCLES) {
      itemVisibilityCounter = 0;

      if (itemVisible) {
        itemVisible = false;
      } else {
        itemVisible = true;
      }
    }
  }

  printCurrentItemsState();

  memset(s, 0, BUF_L);
  snprintf(s, BUF_L, "%s-%s %s, %s ", getItem(D_DAY), getItem(D_MONTH),
           getItem(D_YEAR), getItem(D_WEEKDAY));

  (void)clock_display_text(0, 0, 1, s);

  unsigned char x = 4;
  unsigned char y = 2;

  char secondDivider = ' ';
  if (!clockSetMode) {
    if (rtcDateTime.second % 2 == 0) {
      secondDivider = ':';
    }
  } else {
    secondDivider = ':';
  }

  memset(s, 0, BUF_L);
  snprintf(s, BUF_L, "%s%c%s", getItem(D_HOUR), secondDivider,
           getItem(D_MINUTE));

  (void)clock_display_text(x, y, 2, s);

  memset(s, 0, BUF_L);
  snprintf(s, BUF_L, "%s", getItem(D_SECOND));
  (void)clock_display_text(x + 11, y + 1, 1, s);
}

static int lastSecondValue = -1;
static unsigned char secondsToCount = 0;
void startTimerForSeconds(unsigned char seconds) {
  secondsToCount = seconds;
  lastSecondValue = -1;
}

bool checkIfTimerReached(void) {
  if (secondsToCount == 0) {
    return true;
  }

  /* Only the seconds count here; the shown and edited time stays intact. */
  RTC_DateTime now = {0};
  rtc_GetDateTime(&now);
  if (lastSecondValue == -1) {
    lastSecondValue = now.second;
    return false;
  }
  if (lastSecondValue != now.second) {
    lastSecondValue = now.second;
    secondsToCount--;
  }
  return false;
}
