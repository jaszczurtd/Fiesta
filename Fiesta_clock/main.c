
#include "main.h"

char s[BUF_L + 1];

static unsigned char mode = MODE_CLOCK;
static bool lastIgnition = false;

static void setup(void) {
    wdt_enable( WDTO_2S );

	sbi(DDRC, PC0); //red led
	sbi(DDRC, PC1); //orange led
	sbi(DDRC, PC2);	//blue led

	//ADC6 = voltomierz

	cbi(DDRD, PD0); //increase hour
	cbi(DDRD, PD1); //increase minute
	cbi(DDRD, PD4); //set clock

	cbi(DDRC, PC3); //ignition

    TWI_Init();
    lcd_init(LCD_DISP_ON);    // init lcd and turn on
    lcd_clrscr();

    lcd_charMode(NORMALSIZE);
    lcd_gotoxy(8, 0);
	lcd_puts("FORD");

    lcd_charMode(DOUBLESIZE);
    lcd_gotoxy(4, 2);
	lcd_puts("FIESTA");

    PCF_Init(PCF_TIMER_INTERRUPT_ENABLE);

    temp_initial_read();

    sei();
}

int main(void) {

	setup();

    while(1) {
    	wdt_reset();

    	bool ig = ignition();
    	if(ig != lastIgnition) {
    		lastIgnition = ig;
    		lcd_clrscr();
    	}

    	if(!ig) {
        	clockMainFunction();
    	} else {

    		switch(mode) {
    		case MODE_CLOCK:
            	clockMainFunction();
    			break;
    		case MODE_TEMP:
        		temp_read_display();
    			break;
    		case MODE_VOLT:
    			voltMainFunction();
    			break;
    		}

    		if(setButtonPressed()) {
    			mode++;
    			if(mode > MODE_VOLT) {
    				mode = MODE_CLOCK;
    			}
				lcd_clrscr();
    		}

    	}
    }
}

void redLED(bool state) {
	(state) ? sbi(PORTC, PC0) : cbi(PORTC, PC0);
}
void orangeLED(bool state) {
	(state) ? sbi(PORTC, PC1) : cbi(PORTC, PC1);
}
void blueLED(bool state) {
	(state) ? sbi(PORTC, PC2) : cbi(PORTC, PC2);
}

bool ignition(void) {
	return bit_is_clear(PINC, PC3);
}
bool setButtonPressed(void) {
	bool state = false;

	while(setButton()) {
		wdt_reset();
		state = true;
		blueLED(true);
	}
	blueLED(false);

	return state;
}
bool setButton(void) {
	return bit_is_clear(PIND, PD4);
}
bool setHour(void) {
	return bit_is_clear(PIND, PD0);
}
bool setMinute(void) {
	return bit_is_clear(PIND, PD1);
}

bool anyButton(void) {
	return setButton() || setHour() || setMinute();
}
