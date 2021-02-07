/**
 *
 * zadig driver installer
 * https://rlogiacco.wordpress.com/2016/09/01/usbasp-windows-10/
 * https://zadig.akeo.ie/
 
 libusbK
 usbasp
 *
 
 */

#include "main.h"

#if DEBUG_VAL
int debugval = 0;
#endif

void setup(void) {
    cli();
    
    wdt_enable( WDTO_2S );
    
    TWI_Init();
    PWM_Init(true, false);
    ADC_Init(true);

    clearPorts();
    
    restoreStatusFromEEPROM();
    
    sei();
}

int main(void) {
    
    setup();
    
    lcd_init(LCD_DISP_ON);    // init lcd and turn on

    lcd_clrscr();

	int p = 0;
	int b = 0;
	int c = 0;

	char buf[128];

    while(1) {
        if(!storeStatusToEEPROM()) {
        }

        memset (buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf) - 1, "HELLO %d", p++);

        lcd_gotoxy(0,0);          // set cursor to first column at line 3
        lcd_puts(buf);  // put string from RAM to display (TEXTMODE) or buffer (GRAPHICMODE)

        memset (buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf) - 1, "Hello %d", b += 2);

        lcd_gotoxy(0,1);          // set cursor to first column at line 3
        lcd_puts(buf);  // put string from RAM to display (TEXTMODE) or buffer (GRAPHICMODE)

        memset (buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf) - 1, "AaBbCc %d", c += 3);

        lcd_gotoxy(0,2);          // set cursor to first column at line 3
        lcd_puts(buf);  // put string from RAM to display (TEXTMODE) or buffer (GRAPHICMODE)

    	wdt_reset();

	_delay_ms(MAIN_DELAY_TIME);
    }

    return 0;
}
