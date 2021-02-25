/*
ds18b20 lib 0x02

copyright (c) Davide Gironi, 2012

Released under GPLv3.
Please refer to LICENSE file for licensing information.
*/

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

#include "ds18b20.h"

#ifdef DS18B20_PORTA
#define	DS18B20_PORT PORTA
#define	DS18B20_DDR DDRA
#define	DS18B20_PIN PINA
#endif

#ifdef DS18B20_PORTB
#define	DS18B20_PORT PORTB
#define	DS18B20_DDR DDRB
#define	DS18B20_PIN PINB
#endif

#ifdef DS18B20_PORTC
#define	DS18B20_PORT PORTC
#define	DS18B20_DDR DDRC
#define	DS18B20_PIN PINC
#endif

#ifdef DS18B20_PORTD
#define	DS18B20_PORT PORTD
#define	DS18B20_DDR DDRD
#define	DS18B20_PIN PIND
#endif

bool anyButton(void);

static unsigned char DS18B20_DQ = 0;

void ds18b20_setPin(unsigned char pin) {
	DS18B20_DQ = pin;
}

/*
 * ds18b20 init
 */
uint8_t ds18b20_reset(void) {
    uint8_t i;

    //low for 480us
    DS18B20_PORT &= ~ (1<<DS18B20_DQ); //low
    DS18B20_DDR |= (1<<DS18B20_DQ); //output
    _delay_us(480);
    if(anyButton()) {
    	return 1;
    }

    //release line and wait for 60uS
    DS18B20_DDR &= ~(1<<DS18B20_DQ); //input
    _delay_us(60);
    if(anyButton()) {
    	return 1;
    }

    //get value and wait 420us
    i = (DS18B20_PIN & (1<<DS18B20_DQ));
    _delay_us(420);
    if(anyButton()) {
    	return 1;
    }

    //return the read value, 0=ok, 1=error
    return i;
}

/*
 * write one bit
 */
void ds18b20_writebit(uint8_t bit){
    //low for 1uS
    DS18B20_PORT &= ~ (1<<DS18B20_DQ); //low
    DS18B20_DDR |= (1<<DS18B20_DQ); //output
    _delay_us(1);

    //if we want to write 1, release the line (if not will keep low)
    if(bit)
        DS18B20_DDR &= ~(1<<DS18B20_DQ); //input

    //wait 60uS and release the line
    _delay_us(60);
    DS18B20_DDR &= ~(1<<DS18B20_DQ); //input
}

/*
 * read one bit
 */
uint8_t ds18b20_readbit(void){
    uint8_t bit=0;

    //low for 1uS
    DS18B20_PORT &= ~ (1<<DS18B20_DQ); //low
    DS18B20_DDR |= (1<<DS18B20_DQ); //output
    _delay_us(1);

    //release line and wait for 14uS
    DS18B20_DDR &= ~(1<<DS18B20_DQ); //input
    _delay_us(14);

    //read the value
    if(DS18B20_PIN & (1<<DS18B20_DQ))
        bit=1;

    //wait 45uS and return read value
    _delay_us(45);
    return bit;
}

/*
 * write one byte
 */
void ds18b20_writebyte(uint8_t byte){
    uint8_t i=8;
    while(i--){
        ds18b20_writebit(byte&1);
        byte >>= 1;

        if(anyButton()) {
        	break;
        }
    }
}

/*
 * read one byte
 */
uint8_t ds18b20_readbyte(void){
    uint8_t i=8, n=0;
    while(i--){
        n >>= 1;
        n |= (ds18b20_readbit()<<7);
    }
    return n;
}

/*
 * get temperature
 */
void ds18b20_gettemp(int *a, unsigned char *b, double *native) {
    uint8_t temperature_l;
    uint8_t temperature_h;
    char error = 0;
    double retd = 0;

    #if DS18B20_STOPINTERRUPTONREAD == 1
    cli();
    #endif

    error = ds18b20_reset(); //reset
    if(anyButton() || error > 0) {
		return;
	}
    ds18b20_writebyte(DS18B20_CMD_SKIPROM); //skip ROM
    if(anyButton()) {
		return;
	}

    ds18b20_writebyte(DS18B20_CMD_CONVERTTEMP); //start temperature conversion
    if(anyButton()) {
		return;
	}

	//wait until conversion is complete
    while(!ds18b20_readbit()) {
        if(anyButton()) {
    		return;
    	}
    }

    error = ds18b20_reset(); //reset
    if(anyButton() || error > 0) {
		return;
	}
    ds18b20_writebyte(DS18B20_CMD_SKIPROM); //skip ROM
    if(anyButton()) {
		return;
	}
    ds18b20_writebyte(DS18B20_CMD_RSCRATCHPAD); //read scratchpad
    if(anyButton()) {
		return;
	}

    //read 2 byte from scratchpad
    temperature_l = ds18b20_readbyte();
    if(anyButton()) {
		return;
	}
    temperature_h = ds18b20_readbyte();
    if(anyButton()) {
		return;
	}

    #if DS18B20_STOPINTERRUPTONREAD == 1
    sei();
    #endif

    //convert the 12 bit value obtained
    retd = ( ( temperature_h << 8 ) + temperature_l ) * 0.0625;
    if(native != NULL) {
    	*native = retd;
    }

    int t1 = (int)retd;
    if(t1 > -128) {
    	if(a != NULL) {
    		*a = t1;
    	}
        int t2 = (int) (((double)retd - t1) * 10);
        if(b != NULL) {
			if(t2 >= 0) {
				*b = t2;
			} else {
				*b = 0;
			}
        }
    }
}


