
#include "main.h"

char s[BUF_L + 1];

static unsigned char mode_ignition = MODE_CLOCK;

static bool lastIgnition = false;
static bool idleTimer = false;
static bool blinkLED = false;
static int idleLEDCycles = 0;
static int changeModeCycles = 0;

static void setDisplaytMode(void);
static void setup_runtime(void);

static void helloMessage(void) {
	lcd_init(LCD_DISP_ON);

	lcd_clrscr();

    lcd_charMode(NORMALSIZE);
    lcd_gotoxy(8, 0);
	lcd_puts("FORD");

    lcd_charMode(DOUBLESIZE);
    lcd_gotoxy(4, 2);
	lcd_puts("FIESTA");
}

static void setup_runtime(void) {
	hal_watchdog_enable(CFG_WATCHDOG_TIMEOUT_MS, false);

	hal_gpio_set_mode(PIN_LED_RED, HAL_GPIO_OUTPUT);
	hal_gpio_set_mode(PIN_LED_ORANGE, HAL_GPIO_OUTPUT);
	hal_gpio_set_mode(PIN_LED_BLUE, HAL_GPIO_OUTPUT);
	redLED(false);
	orangeLED(false);
	blueLED(false);

	hal_gpio_set_mode(PIN_BUTTON_HOUR, HAL_GPIO_INPUT_PULLUP);
	hal_gpio_set_mode(PIN_BUTTON_MINUTE, HAL_GPIO_INPUT_PULLUP);
	hal_gpio_set_mode(PIN_BUTTON_SET, HAL_GPIO_INPUT_PULLUP);

	hal_gpio_set_mode(PIN_IGNITION, HAL_GPIO_INPUT_PULLUP);

	hal_adc_set_resolution((uint8_t)CFG_ADC_RESOLUTION_BITS);

	TWI_Init();

	idleTimer = false;
	blinkLED = false;
	idleLEDCycles = 0;
	changeModeCycles = 0;
	mode_ignition = MODE_CLOCK;

    helloMessage();

    PCF_Init(PCF_TIMER_INTERRUPT_ENABLE);

    temp_initial_read();

    lcd_clrscr();
}

void setup_c(void) {
	setup_runtime();
}

void loop_c(void) {
	hal_watchdog_feed();

	bool ig = ignition();
	if (ig != lastIgnition) {
		lastIgnition = ig;
		blinkLED = false;
		blueLED(false);
		changeModeCycles = 0;

		helloMessage();

		temp_initial_read();

		lcd_clrscr();
	}

	if (idleTimer) {
		if (checkIfTimerReached()) {
			lcd_sleep(true);
			blinkLED = true;
		}

		if (blinkLED) {
			if (idleLEDCycles++ > (CFG_IDLE_LED_OFF_CYCLES + CFG_IDLE_LED_ON_CYCLES)) {
				idleLEDCycles = 0;
			}

			blueLED(idleLEDCycles < CFG_IDLE_LED_ON_CYCLES);
		}

	} else {
		lcd_sleep(false);
	}

	if (!ig) {
		orangeLED(false);
		redLED(false);

		if (!idleTimer) {
			idleTimer = true;
			startTimerForSeconds((unsigned char)CFG_IDLE_SECONDS);
		}

		if (anyButton() || isClockSetMode()) {
			idleTimer = false;
			blinkLED = false;
			blueLED(false);
		}

		if (!blinkLED) {
			clockMainFunction();
		}

	} else {
		idleTimer = false;
		setDisplaytMode();
    }
}

static void setDisplaytMode(void) {
	switch (mode_ignition) {
		case MODE_CLOCK:
			clockMainFunction();
			break;
		case MODE_TEMP:
			temp_read_display();
			break;
		case MODE_VOLT:
			voltMainFunction();
			break;
		default:
			mode_ignition = MODE_CLOCK;
			break;
	}

	if (ignition()) {
		if (setButtonPressed()) {
			changeModeCycles = 0;
			mode_ignition++;
			if (mode_ignition > MODE_VOLT) {
				mode_ignition = MODE_CLOCK;
			}
			lcd_clrscr();
		}

		int countVal = CFG_CHANGE_MODE_CYCLES;
		switch (mode_ignition) {
			case MODE_TEMP:
				countVal = CFG_CHANGE_MODE_CYCLES / 6;
				break;
			case MODE_VOLT:
				countVal = CFG_CHANGE_MODE_CYCLES / 4;
				break;
			default:
				break;
		}

		if (countVal < changeModeCycles++) {
			changeModeCycles = 0;
			mode_ignition++;
			if (mode_ignition > MODE_VOLT) {
				mode_ignition = MODE_CLOCK;
			}
			lcd_clrscr();
		}
	}
}

void redLED(bool state) {
	const bool level = state ? LED_ACTIVE_LEVEL : !LED_ACTIVE_LEVEL;
	hal_gpio_write(PIN_LED_RED, level);
}

void orangeLED(bool state) {
	const bool level = state ? LED_ACTIVE_LEVEL : !LED_ACTIVE_LEVEL;
	hal_gpio_write(PIN_LED_ORANGE, level);
}

void blueLED(bool state) {
	const bool level = state ? LED_ACTIVE_LEVEL : !LED_ACTIVE_LEVEL;
	hal_gpio_write(PIN_LED_BLUE, level);
}

bool ignition(void) {
	return hal_gpio_read(PIN_IGNITION) == IGNITION_ACTIVE_LEVEL;
}

bool setButtonPressed(void) {
	bool state = false;

	while (setButton()) {
		hal_watchdog_feed();
		state = true;
		blueLED(true);
	}
	blueLED(false);

	return state;
}

bool setButton(void) {
	return hal_gpio_read(PIN_BUTTON_SET) == BUTTON_ACTIVE_LEVEL;
}

bool setHour(void) {
	return hal_gpio_read(PIN_BUTTON_HOUR) == BUTTON_ACTIVE_LEVEL;
}

bool setMinute(void) {
	return hal_gpio_read(PIN_BUTTON_MINUTE) == BUTTON_ACTIVE_LEVEL;
}

bool anyButton(void) {
	return setButton() || setHour() || setMinute();
}
