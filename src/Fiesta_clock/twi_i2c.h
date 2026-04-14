//
//  i2c.h
//  Index
//
//  Created by Marcin Kielesiński on 26/01/2020.
//

#ifndef i2c_h
#define i2c_h

#include <stdio.h>
#include "utils.h"

#define MASTER_TRANSMITTER 0
#define MASTER_RECEIVER 1
#define TRANSMISSION_SUCCESS -1
#define TRANSMISSION_ERROR -2

void TWI_Init(void);
void TWI_Start(void);
void TWI_Stop(void);
unsigned char TWI_Read(unsigned char ack);
void TWI_Write(unsigned char byte);
unsigned char TWI_address(unsigned char address, bool masterMode);


#endif /* i2c_h */
