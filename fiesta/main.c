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

uint8_t u8x8_byte_sw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_d_ssd1306_128x32_univision(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);



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

u8g2_t u8g2;

int main(void) {
    
    setup();

    u8g2_Setup_ssd1306_128x32_univision_f(&u8g2, U8G2_R0, u8x8_byte_sw_i2c, u8x8_d_ssd1306_128x32_univision);
    u8g2_InitDisplay(&u8g2);
	u8g2_SetPowerSave(&u8g2, 0);

	/* full buffer example, setup procedure ends in _f */
	u8g2_ClearBuffer(&u8g2);
	u8g2_SetFontDirection(&u8g2, 0);
	u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
	u8g2_DrawStr(&u8g2, 1, 1, "U8g2 on AVR");
	u8g2_SendBuffer(&u8g2);

	int p = 0;
	char buf[128];

    while(1) {
        if(!storeStatusToEEPROM()) {
        }

        memset (buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf) - 1, "HELLO WORLD %d", p++);

    	wdt_reset();

	_delay_ms(MAIN_DELAY_TIME);
    }

    return 0;
}
