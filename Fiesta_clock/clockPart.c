//
//  clockPart.c
//  Index
//
//  Created by Marcin Kielesiński on 27/01/2020.
//

#include "clockPart.h"

#define CLOCK_OPERATION_DELAY 120
#define ITEM_VISIBILITY_CYCLES_COUNTER 10

static PCF_DateTime pcfDateTime;
static bool clockSetMode = false;

static unsigned char activeItem = 0;
static bool itemVisible = true;
static int itemVisibilityCounter = 0;

static char day[4] = 		{0};
static char month[4] =		{0};
static char year[5] = 		{0};
static char weekday[7] = 	{0};
static char hour[4] = 		{0};
static char minute[4] = 	{0};
static char second[4] = 	{0};

#define D_DAY 		0
#define D_MONTH 	1
#define D_YEAR 		2
#define D_WEEKDAY	3
#define D_HOUR 		4
#define D_MINUTE 	5
#define D_SECOND 	6

#define CLOCK_SETTABLE_ITEMS 7

static const char *items[CLOCK_SETTABLE_ITEMS] = {
    day, month, year, weekday, hour, minute, second
};

void printCurrentItemsState(void) {
    snprintf(day, sizeof(day), "%02d", pcfDateTime.day);
    snprintf(month, sizeof(month), "%02d", pcfDateTime.month);
    snprintf(year, sizeof(year), "%d", pcfDateTime.year);
    snprintf(hour, sizeof(hour), "%02d", pcfDateTime.hour);
    snprintf(minute, sizeof(minute), "%02d", pcfDateTime.minute);
    snprintf(second, sizeof(second), "%02d", pcfDateTime.second);

    switch(pcfDateTime.weekday) {
        case 0:
            snprintf(weekday, sizeof(weekday) - 1, "pon.");
            break;
        case 1:
        	snprintf(weekday, sizeof(weekday) - 1, "wt.");
            break;
        case 2:
        	snprintf(weekday, sizeof(weekday) - 1, "sr.");
            weekday[0] = 0x7f;
            break;
        case 3:
        	snprintf(weekday, sizeof(weekday) - 1, "czw.");
            break;
        case 4:
        	snprintf(weekday, sizeof(weekday) - 1, "pt.");
            break;
        case 5:
        	snprintf(weekday, sizeof(weekday) - 1, "sob.");
            break;
        case 6:
        	snprintf(weekday, sizeof(weekday) - 1, "niedz.");
            break;
    }
}

const char *getItem(unsigned char number) {
    if(activeItem == number) {

    	static char *empty = NULL;

    	switch(number) {
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

void pause(void) {
	_delay_ms(CLOCK_OPERATION_DELAY);
}

void setItem(bool increase) {
    switch(activeItem) {
        case D_DAY:
            if(increase) {
                if(pcfDateTime.day < 31) {
                    pcfDateTime.day++;
                } else {
                    pcfDateTime.day = 1;
                }
            } else {
                if(pcfDateTime.day > 1) {
                    pcfDateTime.day--;
                } else {
                    pcfDateTime.day = 31;
                }
            }
            break;
            
        case D_MONTH:
            if(increase) {
                if(pcfDateTime.month < 12) {
                    pcfDateTime.month++;
                } else {
                    pcfDateTime.month = 1;
                }
            } else {
                if(pcfDateTime.month > 1) {
                    pcfDateTime.month--;
                } else {
                    pcfDateTime.month = 12;
                }
            }
            break;
            
        case D_YEAR:
            if(increase) {
                if(pcfDateTime.year < PCF_MAX_YEAR) {
                    pcfDateTime.year++;
                } else {
                    pcfDateTime.year = PCF_MIN_YEAR;
                }
            } else {
                if(pcfDateTime.year > PCF_MIN_YEAR) {
                    pcfDateTime.year--;
                } else {
                    pcfDateTime.year = PCF_MAX_YEAR;
                }
            }
            break;
            
        case D_HOUR:
            if(increase) {
                if(pcfDateTime.hour < 23) {
                    pcfDateTime.hour++;
                } else {
                    pcfDateTime.hour = 0;
                }
            } else {
                if(pcfDateTime.hour > 0) {
                    pcfDateTime.hour--;
                } else {
                    pcfDateTime.hour = 23;
                }
            }
            break;
            
        case D_MINUTE:
            if(increase) {
                if(pcfDateTime.minute < 59) {
                    pcfDateTime.minute++;
                } else {
                    pcfDateTime.minute = 0;
                }
            } else {
                if(pcfDateTime.minute > 0) {
                    pcfDateTime.minute--;
                } else {
                    pcfDateTime.minute = 59;
                }
            }
            break;

        case D_SECOND:
            if(increase) {
                if(pcfDateTime.second < 59) {
                    pcfDateTime.second++;
                } else {
                    pcfDateTime.second = 0;
                }
            } else {
                if(pcfDateTime.second > 0) {
                    pcfDateTime.second--;
                } else {
                    pcfDateTime.second = 59;
                }
            }
            break;

        case D_WEEKDAY:
            if(increase) {
                if(pcfDateTime.weekday < 6) {
                    pcfDateTime.weekday++;
                } else {
                    pcfDateTime.weekday = 0;
                }
            } else {
                if(pcfDateTime.weekday > 0) {
                    pcfDateTime.weekday--;
                } else {
                    pcfDateTime.weekday = 6;
                }
            }
            break;

    }
    setItemVisible();
    pause();
}

void setClockSetMode(bool enabled) {
    clockSetMode = enabled;
    if(clockSetMode) {
        activeItem = 0;
        setItemVisible();
    }
}

void getTime(void) {
    PCF_GetDateTime(&pcfDateTime);
}

void clockMainFunction(void) {

    if(!clockSetMode) {
    
    	getTime();
        
        if(!ignition() && setButtonPressed()) {
        	pause();
            setClockSetMode(true);
            setItemVisible();
            return;
        }
        
    } else {
    	 if(!ignition()) {

    		 if(setButtonPressed()) {
				pause();

                 if(++activeItem > CLOCK_SETTABLE_ITEMS - 1) {
                     setClockSetMode(false);

                     PCF_SetDateTime(&pcfDateTime);
                     return;
                 }
    		 } else if(setHour()) {
                 setItem(true);
    		 } else if(setMinute()) {
                 setItem(false);
    		 }
    	 }
        
        if(itemVisibilityCounter++ > ITEM_VISIBILITY_CYCLES_COUNTER) {
            itemVisibilityCounter = 0;
            
            if(itemVisible) {
                itemVisible = false;
            } else {
                itemVisible = true;
            }
        }
    }

	printCurrentItemsState();

    lcd_gotoxy(0, 0);
    memset(s, 0, BUF_L);
    snprintf(s, BUF_L, "%s-%s %s, %s ",
    		getItem(D_DAY),
    		getItem(D_MONTH),
			getItem(D_YEAR),
			getItem(D_WEEKDAY));

    lcd_charMode(NORMALSIZE);
    lcd_puts(s);

    unsigned char x = 4;
    unsigned char y = 2;

	lcd_gotoxy(x, y);

	char secondDivider = ' ';
	if(!clockSetMode) {
		if(pcfDateTime.second %2 == 0) {
			secondDivider = ':';
		}
	} else {
		secondDivider = ':';
	}

    memset(s, 0, BUF_L);
    snprintf(s, BUF_L, "%s%c%s",
    		getItem(D_HOUR),
			secondDivider,
			getItem(D_MINUTE));

    lcd_charMode(DOUBLESIZE);
    lcd_puts(s);

    lcd_gotoxy(x + 11, y + 1);
    memset(s, 0, BUF_L);
    snprintf(s, BUF_L, "%s", getItem(D_SECOND));
    lcd_charMode(NORMALSIZE);
    lcd_puts(s);
}

static unsigned char lastSecondValue = -1;
static unsigned char secondsToCount = 0;
void startTimerForSeconds(unsigned char seconds) {
    secondsToCount = seconds;
    lastSecondValue = -1;
}

bool checkIfTimerReached(void) {
    if(secondsToCount == 0) {
        return true;
    }
    
    PCF_GetDateTime(&pcfDateTime);
    if(lastSecondValue == -1) {
        lastSecondValue = pcfDateTime.second;
        return false;
    }
    if(lastSecondValue != pcfDateTime.second) {
        lastSecondValue = pcfDateTime.second;
        secondsToCount--;
    }
    return false;
}
