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
    
    OLED_Init();  //initialize the OLED
	OLED_Clear(); //clear the display (for good measure)




	int p = 0;
	char buf[128];

    while(1) {
        if(!storeStatusToEEPROM()) {
        }

        memset (buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf) - 1, "HELLO WORLD %d", p++);
    	OLED_SetCursor(0, 0);        //set the cursor position to (0, 0)
    	OLED_Printf(buf); //Print out some text

    	wdt_reset();

	_delay_ms(MAIN_DELAY_TIME);
    }

    return 0;
}
