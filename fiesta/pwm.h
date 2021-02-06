//
//  pwm.h
//  Index
//
//  Created by Marcin Kielesiński on 28/12/2019.
//

#ifndef pwm_h
#define pwm_h

#include <string.h>
#include <avr/io.h>
#include <util/delay.h>
#include "utils.h"

void PWM_Init(bool channel0, bool channel1);
void PWM_Increase(bool channel0, bool channel1, int val);
void PWM_Decrease(bool channel0, bool channel1, int val);
void PWM_SetValue(bool channel0, bool channel1, int value);

#endif /* pwm_h */
