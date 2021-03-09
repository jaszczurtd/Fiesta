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

char s[128];

void redLED(bool state) {
	//(state) ? sbi(PORTA, PA0) : cbi(PORTA, PA0);
}

void setup(void) {
    cli();
    
   // wdt_enable( WDTO_2S );
    
    //sbi(DDRA, PA0); //red led

    //TWI_Init();
    //PWM_Init(true, false);
    //ADC_Init(true);

    //clearPorts();
    
    //restoreStatusFromEEPROM();
    
    sei();
}

int main(void) {
    
    setup();
    
	int p = 0;
	int b = 0;
	int c = 0;

    spi_init();
    st7735_init();

    st7735_set_orientation(ST7735_LANDSCAPE);
    st7735_fill_rect(0, 0, 160, 128, ST7735_COLOR_BLACK);

    st7735_draw_text(0, 16, "Na ford-techu", &FreeSans, 1, ST7735_COLOR_RED);
    st7735_draw_text(0, 32, "ostatnioaaaa", &FreeSans, 1, ST7735_COLOR_BLUE);
    st7735_draw_text(0, 64, "strasznie", &FreeSans, 1, ST7735_COLOR_CYAN);
    st7735_draw_text(0, 80, "wieje", &FreeSans, 1, ST7735_COLOR_GREEN);
    st7735_draw_text(0, 100, "chujem", &FreeSans, 1, ST7735_COLOR_YELLOW);

//    st7735_draw_text(5, 30, "This is\njust a Test xyz\nJaszczur", &FreeSans, 1, ST7735_COLOR_WHITE);

    while(1) {
    	//wdt_reset();


    	redLED(false);
    	_delay_ms(MAIN_DELAY_TIME);
    	redLED(true);
    	_delay_ms(MAIN_DELAY_TIME);


    }

    return 0;
}
