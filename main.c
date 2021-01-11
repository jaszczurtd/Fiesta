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
    
    while(1) {



        if(!storeStatusToEEPROM()) {
            _delay_ms(MAIN_DELAY_TIME);
        }
    }
    
    return 0;
}
