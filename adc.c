//
//  adc.c
//  Index
//
//  Created by Marcin Kielesiński on 26/12/2019.
//

#include "adc.h"

static int value;
static bool byInterrupt = false;

void ADC_Init(bool interrupt) {
    byInterrupt = interrupt;
    
    bitSet(ADMUX, REFS0);
    bitClear(ADMUX, REFS1);
    
    bitClear(ADMUX, MUX0);
    bitClear(ADMUX, MUX1);
    bitClear(ADMUX, MUX2);

    bitSet(ADCSRA, ADEN | ADPS0 | ADPS1 | ADPS2);
    
    if(interrupt) {
        bitSet(ADCSRA, ADSC);
        bitSet(ADCSRA, ADIE);
    }
    
    value = 0;
}

int getADCValue(void) {
    
    if(byInterrupt) {
        bitSet(ADCSRA, ADIE);
        bitSet(ADCSRA, ADSC);

        return value;
    } else {
        bitSet(ADCSRA, ADSC);
        while(ADCSRA & (1<<ADSC));
        
        return ADC;
    }
}

ISR(ADC_vect) {
    value = ADC;
}
