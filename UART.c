//
//  UART.c
//  Index
//
//  Created by Marcin Kielesiński on 19/12/2019.
//

#include "UART.h"

void UART_Init(void) {
    int myubrr = MYUBRR;
    UBRRH = (unsigned char)(myubrr>>8);    // Ustalenie prędkości transmisji
    UBRRL = (unsigned char)myubrr;

    UCSRB = (1<<RXEN)|(1<<TXEN)|(1<<RXCIE);     // Włączenie nadajnika, odbiornika,
                                                   // odblokowanie przerwania od odbioru
    // Format ramki: 8 bitów danych, 1 bit stopu, włączony tryb nieparzystości
    UCSRC = (1<<URSEL)|(3<<UCSZ0)|(1<<UPM1);
}

unsigned char UART_Receive(void) {
    while ( !(UCSRA & (1<<RXC)) );    // Wait for data to be received //
    return UDR;                        // Get and return received data from buffer //
}                                    // (zmienna data przyjmuje wartosc UDR)

void UART_Sent(unsigned char data) {
    while ( !( UCSRA & (1<<UDRE)) );    // Wait for empty transmit buffer //
    UDR = data;                            // Put data into buffer, sends the data //
}

