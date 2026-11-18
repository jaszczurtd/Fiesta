//
//  clockPart.c
//  Index
//
//  Created by Marcin Kielesiński on 27/01/2020.
//

#include "clockPart.h"

#include <hal/time/jh_calendar.h>

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

/* Days of the edited month; the year limits keep it inside the calendar. */
static unsigned char daysInMonth(void) {
  uint8_t days = 31u;
  if (rtcDateTime.year < 0 || rtcDateTime.year > (int)UINT16_MAX ||
      jh_calendar_days_in_month((uint16_t)rtcDateTime.year, rtcDateTime.month,
                                &days) != HAL_OK) {
    days = 31u;
  }
  return days;
}

void setItem(bool increase) {
  switch (activeItem) {
  case D_DAY:
    if (increase) {
      if (rtcDateTime.day < daysInMonth()) {
        rtcDateTime.day++;
      } else {
        rtcDateTime.day = 1;
      }
    } else {
      if (rtcDateTime.day > 1) {
        rtcDateTime.day--;
      } else {
        rtcDateTime.day = daysInMonth();
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
  /* A shorter month or a common year moves the day to the last valid one, so
   * the stored date always exists. */
  if (rtcDateTime.day > daysInMonth()) {
    rtcDateTime.day = daysInMonth();
  }
  /* The weekday follows the date; it is shown but not set. */
  clockTimeUpdateWeekday(&rtcDateTime);
  setItemVisible();
}

/* Set mode: the button that steps the edited item (0 none, 1 hour = up,
 * 2 minute = down), since when it is held and when it last stepped. */
static uint8_t heldButton = 0u;
static uint32_t heldSinceMs = 0u;
static uint32_t lastStepMs = 0u;

/* One step when a button goes down; held, it steps again every
 * CFG_CLOCK_REPEAT_INTERVAL_MS once it was held CFG_CLOCK_REPEAT_DELAY_MS, so
 * a short press always gives exactly one step. Timed by hal_millis(). */
static bool buttonStep(uint8_t button) {
  const uint32_t nowMs = hal_millis();
  if (button != heldButton) {
    heldButton = button;
    heldSinceMs = nowMs;
    lastStepMs = nowMs;
    return button != 0u;
  }
  if (button == 0u || nowMs - heldSinceMs < CFG_CLOCK_REPEAT_DELAY_MS ||
      nowMs - lastStepMs < CFG_CLOCK_REPEAT_INTERVAL_MS) {
    return false;
  }
  lastStepMs = nowMs;
  return true;
}

void setClockSetMode(bool enabled) {
  clockSetMode = enabled;
  heldButton = 0u; /* a button held across the change starts a new press */
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

      (void)clock_display_restart();
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
        pause();
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
        pause();
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
        (void)clock_display_restart();
        return;
      }
    } else {
      /* Hour steps the item up, minute down; hour wins when both are held. */
      const uint8_t button = setHour() ? 1u : setMinute() ? 2u : 0u;
      if (buttonStep(button)) {
        setItem(button == 1u);
      }
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

  /* The weekday name varies in length: erase the rest of the row. */
  (void)clock_display_field(0, 0, 1, CLOCK_DISPLAY_COLUMNS, s);

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
