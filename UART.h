//
//  UART.h
//  Index
//
//  Created by Marcin Kielesiński on 19/12/2019.
//

#ifndef UART_h
#define UART_h

#define BAUD 2400                // Prędkość transmisji 2400
#define MYUBRR F_CPU/16/BAUD-1    // Wartość rejestru UBRR

#include <stdio.h>
#include <stdbool.h>

#include <string.h>
#include <avr/io.h>
#include <util/delay.h>
#include "utils.h"

void UART_Init(void);
unsigned char UART_Receive(void);
void UART_Sent(unsigned char data);

#endif /* UART_h */
