//
//  utils.h
//  Index
//
//  Created by Marcin Kielesiński on 07/12/2019.
//

#ifndef utils_h
#define utils_h

#define DS18B20_ENABLED false

#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/delay.h>


#ifdef WDT_ENABLE
#define WDR()    wdt_reset()
#else
#define WDR()
#endif

#define WR wdt_reset

#define NOP() __asm__ __volatile__ ("nop")

#ifndef CYCLES_PER_US
#define CYCLES_PER_US ((F_CPU+500000)/1000000)
#endif

#ifndef GCC_VERSION
#define GCC_VERSION (__GNUC__ * 1000 + __GNUC_MINOR__)
#endif

#ifndef cbi
#define cbi(PORT, BIT) (_SFR_BYTE(PORT) &= ~_BV(BIT))
#endif

#ifndef sbi
#define sbi(PORT, BIT) (_SFR_BYTE(PORT) |= _BV(BIT))
#endif

#ifndef tbi
#define tbi(PORT, BIT)    (_SFR_BYTE(PORT) ^= _BV(BIT))
#endif

#define DDR(x) (_SFR_IO8(_SFR_IO_ADDR(x)-1))

#define PIN(x) (_SFR_IO8(_SFR_IO_ADDR(x)-2))

#define EEPROM __attribute__((section(".eeprom")))

#define ESTR(s) ({static char __c[] EEPROM = (s); __c;})

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

#ifndef TRUE
#define TRUE    1
#endif

#ifndef FALSE
#define FALSE    0
#endif

#ifndef _UBRR_
#ifdef    UBRR
#define _UBRR_    UBRR
#endif
#endif

#ifndef _UBRR_
#ifdef    UBRR0
#define _UBRR_    UBRR0
#endif
#endif

#ifndef _UBRR_
#ifdef    UBRR0L
#define _UBRR_    UBRR0L
#endif
#endif

#ifdef    UBRRH
#define _UBRRH_    UBRRH
#endif

#ifdef    UBRR0H
#define _UBRRH_    UBRR0H
#endif

#ifdef    UBRRHI
#define _UBRRH_    UBRRHI
#endif

#ifdef    UDR
#define _UDR_    UDR
#endif

#ifdef    UDR0
#define _UDR_    UDR0
#endif

#ifdef    UCR
#define _UCR_    UCR
#endif

#ifdef    UCSRB
#define _UCR_    UCSRB
#endif

#ifdef    UCSR0B
#define _UCR_    UCSR0B
#endif

#ifdef    USR
#define _USR_    USR
#endif

#ifdef    UCSRA
#define _USR_    UCSRA
#endif

#ifdef    UCSR0A
#define _USR_    UCSR0A
#endif

#ifdef SIG_UART0_RECV
#define SIG_UART_RECV          SIG_UART0_RECV
#endif

#ifdef SIG_UART0_DATA
#define SIG_UART_DATA          SIG_UART0_DATA
#endif

#ifdef SIG_UART0_TRANS
#define SIG_UART_TRANS         SIG_UART0_TRANS
#endif



// ---------------------------------------------------------------

typedef unsigned char      u08;    ///< Typ 8 bitowy bez znaku
typedef          char      s08;    ///< Typ 8 bitowy ze znakiem
typedef unsigned short     u16;    ///< Typ 16 bitowy bez znaku
typedef          short     s16;    ///< Typ 16 bitowy ze znakiem
typedef unsigned long      u32;    ///< Typ 32 bitowy bez znaku
typedef          long      s32;    ///< Typ 32 bitowy ze znakiem
typedef unsigned long long u64;    ///< Typ 64 bitowy bez znaku
typedef          long long s64;    ///< Typ 64 bitowy ze znakiem

// ---------------------------------------------------------------

typedef unsigned char      UCHAR;    ///< Typ 8 bitowy bez znaku
typedef unsigned short     WORD;    ///< Typ 16 bitowy bez znaku
typedef unsigned long      DWORD;    ///< Typ 32 bitowy bez znaku
typedef char*             LPCTSTR;    ///< Wskaünik do ≥aÒcucha znakÛw
// ---------------------------------------------------------------


#ifndef bitClear
#define bitClear(dest, bit) dest &= ~(_BV(bit))
#endif

#ifndef bitSet
#define bitSet(dest, bit) dest |= _BV(bit)
#endif

#ifndef boolP
#define boolP(x) (x) ? "true" : "false"
#endif

void delay_ms(int ms);
int binatoi(char *s);
char *decToBinary(int n);
unsigned char BinToBCD(unsigned char bin);
unsigned char reverse(unsigned char b);
void doubleToDec(double val, int *hi, int *lo);

#endif /* utils_h */
