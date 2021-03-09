#include "spi.h"

void spi_init(void) {

#ifdef __AVR_ATmega32__
	// Set MOSI and SCK, SS/CS output, all others input
	DDRB |= (1 << DDB5) | (1 << DDB7) | (1 << DDB4);
	// Enable SPI, Master, set clock rate fck/4, mode 0
	SPCR = (1 << SPE)  | (1 << MSTR) | (1 << SPR0);

	// Set SS/CS
	PORTB |= (1 << DDB4);
#endif

#ifdef __AVR_ATmega328P__
	// Set MOSI and SCK, SS/CS output, all others input
	DDRB = (1<<DDB3) | (1<<DDB5) | (1<<DDB2);
	// Enable SPI, Master, set clock rate fck/4, mode 0
	SPCR = (1 << SPE)  | (1 << MSTR) | (1 << SPR0);

	// Set SS/CS
	PORTB |= (1 << DDB2);
#endif
}
