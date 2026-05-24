#ifndef TWI_I2C_H_
#define TWI_I2C_H_

#include <stdbool.h>
#include <stdint.h>

#include "utils.h"

#define MASTER_TRANSMITTER 0
#define MASTER_RECEIVER 1
#define TRANSMISSION_SUCCESS 1
#define TRANSMISSION_ERROR 0

void TWI_Init(void);
void TWI_Start(void);
void TWI_Stop(void);
unsigned char TWI_Read(unsigned char ack);
void TWI_Write(unsigned char byte);
unsigned char TWI_address(unsigned char address, bool masterMode);


#endif /* TWI_I2C_H_ */
