//
//  i2c.c
//  Index
//
//  Created by Marcin Kielesiński on 26/01/2020.
//

#include "twi_i2c.h"

void TWI_Init(void) {
    TWSR &= (~((1<<TWPS1)|(1<<TWPS0)));        // Preskaler = 1  ->> TWPS1=0 TWPS0=0
    TWBR = (((F_CPU / 100000) - 16) / 2);      // ((Fclk/Ftwi)-16)/2
}

void TWI_Start(void) {
    TWCR = (1<<TWINT) | (1<<TWEN) | (1<<TWSTA);
    while (!(TWCR & (1<<TWINT)));
}

void TWI_Stop(void) {
    TWCR = (1<<TWINT) | (1<<TWEN) | (1<<TWSTO);
    while ((TWCR & (1<<TWSTO)));
}

unsigned char TWI_Read(unsigned char ack) {
    TWCR = (1<<TWINT) | (1<<TWEN) | (((ack ? 1 : 0)<<TWEA));
    while (!(TWCR & (1<<TWINT)));
    return TWDR;
}

void TWI_Write(unsigned char byte) {
    TWDR = byte;
    TWCR = (1<<TWINT) | (1<<TWEN);
    while (!(TWCR & (1<<TWINT)));
}

