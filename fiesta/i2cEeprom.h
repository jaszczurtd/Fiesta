//
//  i2cEeprom.h
//  Index
//
//  Created by Marcin Kielesiński on 16/12/2019.
//

#ifndef i2cEeprom_h
#define i2cEeprom_h

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "twi_i2c.h"
#include "utils.h"

#define EEPROM_ADDR_WR 0xa0
#define EEPROM_ADDR_RD 0xa1

#define WRITE_EEPROM_DELAY 100

#define EEPROM_SIZE 4

#define E_VOLUME    0
#define E_OUTPUTS   1
#define E_PROGRAMS  2

bool EEPROMwrite(unsigned char ucAddress, unsigned char ucData);
unsigned char EEPROMread(unsigned char ucAddress);

extern void setStoreStatusFlag(bool value);

bool storeStatusToEEPROM(void);
void restoreStatusFromEEPROM(void);

#endif /* i2cEeprom_h */
