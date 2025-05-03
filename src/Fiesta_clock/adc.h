//
//  adc.h
//  Index
//
//  Created by Marcin Kielesiński on 26/12/2019.
//

#ifndef adc_h
#define adc_h

#include <string.h>
#include <avr/io.h>
#include <util/delay.h>
#include "utils.h"

int getADCValue(unsigned char ADCchannel);

#endif /* adc_h */
