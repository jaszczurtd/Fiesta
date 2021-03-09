#ifndef _SPI_H_
#define _SPI_H_

#include<avr/io.h>

void spi_init(void);

static inline void spi_write(uint8_t byte) {
	char flush_buffer;
	SPDR = byte;			/* Write data to SPI data register */
	while(!(SPSR & (1<<SPIF)));	/* Wait till transmission complete */
	flush_buffer = SPDR;
}

static inline void spi_set_cs(void) {
#ifdef __AVR_ATmega32__
	PORTB |= (1 << PB4); //atmega32
#endif
#ifdef __AVR_ATmega328P__
	PORTB |= (1 << PB2); //atmega328p
#endif
}

static inline void spi_unset_cs(void) {
#ifdef __AVR_ATmega32__
	PORTB &= ~(1 << PB4); //atmega32
#endif
#ifdef __AVR_ATmega328P__
	PORTB &= ~(1 << PB2); //atmega328p
#endif
}


#endif
