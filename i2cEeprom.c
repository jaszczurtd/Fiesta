//
//  i2cEeprom.c
//  Index
//
//  Created by Marcin Kielesiński on 16/12/2019.
//

#include "i2cEeprom.h"

bool EEPROM_start(unsigned char addr) {
    
    do {
        TWI_Start();
        
        //Check status
        if((TWSR & 0xF8) != 0x08) {
            return false;
        }
        
        TWI_Write(addr);
        
    } while((TWSR & 0xF8) != 0x18);
    
    return true;
}

bool EEPROMwrite(unsigned char ucAddress, unsigned char ucData) {
    
    if(EEPROM_start(EEPROM_ADDR_WR)) {
        TWI_Write(ucAddress >> 8);
        TWI_Write(ucAddress);
        TWI_Write(ucData);
        TWI_Stop();
        _delay_ms(12);
        return TRUE;
    }
    return false;
}

unsigned char EEPROMread(unsigned char ucAddress) {
    
    unsigned char data = 0;
    
    if(EEPROM_start(EEPROM_ADDR_WR)) {
        TWI_Write(ucAddress >> 8);
        TWI_Write(ucAddress);
        TWI_Start();
        TWI_Write(EEPROM_ADDR_RD);
        data = TWI_Read(0);
        TWI_Stop();
    }
    
    return data;
}

static int eepromDelay = 0;
static bool eepromWrite = false;
unsigned char MEM[EEPROM_SIZE];

inline void setStoreStatusFlag(bool value) {
    eepromWrite = value;
}

bool storeStatusToEEPROM(void) {
    if(eepromDelay-- <= 0) {
        eepromDelay = WRITE_EEPROM_DELAY;
        
        if(eepromWrite) {
            for(int a = 0; a < EEPROM_SIZE; a++) {
                EEPROMwrite(a, MEM[a]);
            }
            eepromWrite = false;
        }
        return true;
    }
    return false;
}

void restoreStatusFromEEPROM(void) {
    for(int a = 0; a < EEPROM_SIZE; a++) {
        MEM[a] = EEPROMread(a);
    }
}
