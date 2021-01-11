//
//  pcf8574a.h
//  Index
//
//  Created by Marcin Kielesiński on 07/02/2020.
//

#ifndef pcf8574a_h
#define pcf8574a_h

#include <stdio.h>
#include "utils.h"
#include "twi_i2c.h"

#define PCF8574_WRITE_ADDR_A     0x70
//#define PCF8574_WRITE_ADDR_B     0x72
//#define PCF8574_WRITE_ADDR_C     0x74

#define PORT_OUTPUTS    0

void pcf8574writeByte(unsigned char outputs, unsigned char value);
void clearPorts(void);

#endif /* pcf8574a_h */
