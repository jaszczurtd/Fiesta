//
//  pwm.c
//  Index
//
//  Created by Marcin Kielesiński on 28/12/2019.
//

#include "pwm.h"

void PWM_Init(bool channel0, bool channel1) {
    
    if(channel0) {
        bitSet(DDRB, PB3);
        TCCR0 |= (1<<WGM00)|(1<<COM00)|(1<<COM01)|(1<<CS01);
        OCR0 = 0;
    }
    if(channel1) {
        bitSet(DDRD, PB7);
        TCCR2 = (1<<WGM21)|(1<<WGM20)|(3<<COM20)|(5<<CS20);
        OCR2 = 0;
    }

}

void PWM_Increase(bool channel0, bool channel1, int val) {
    if(channel0 && OCR0 < 255) {
        OCR0++;
    }
    if(channel1 && OCR2 < 255) {
        OCR2++;
    }
}

void PWM_Decrease(bool channel0, bool channel1, int val) {
    if(channel0 && OCR0 > 0) {
        OCR0--;
    }
    if(channel1 && OCR2 > 0) {
        OCR2--;
    }
}

void PWM_SetValue(bool channel0, bool channel1, int value) {
    if(channel0) {
        OCR0 = value;
    }
    if(channel1) {
        OCR2 = value;
    }
}
