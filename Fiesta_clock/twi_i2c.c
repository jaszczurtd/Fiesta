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

unsigned char TWI_address(unsigned char address, bool masterMode) {
    bool status = 0;

    TWDR = (address << 1);
    /* clear start command to release bus as master */
    TWCR &= ~(1 << TWSTA);
    /* clear interrupt flag */
    TWCR |=  (1 << TWINT);

    /* wait until address transmitted */
    while (!(TWCR & (1 << TWINT)));

    if (masterMode == MASTER_TRANSMITTER) {
        switch (TWSR & 0xF8) {
            /* address|write sent and ACK returned */
            case 0x18:
                status = TRANSMISSION_SUCCESS;
                break;

           /* address|write sent and NACK returned slave */
           case 0x20:
                status = TRANSMISSION_ERROR;
                break;

            /* address|write sent and bus failure detected */
            case 0x38:
                status = TRANSMISSION_ERROR;
                break;

            default:
                status = TRANSMISSION_ERROR;
                break;
        }
    } else if (masterMode == MASTER_RECEIVER) {
        switch (TWSR & 0xF8) {
            /* address|read sent and ACK returned */
            case 0x40:
                status = TRANSMISSION_SUCCESS;
                break;

            /* address|read sent and NACK returned */
            case 0x48:
                status = TRANSMISSION_ERROR;
                break;

            case 0x38:
                status = TRANSMISSION_ERROR;
                break;

            default:
                status = TRANSMISSION_ERROR;
                break;
        }
    }

    return status;
}

