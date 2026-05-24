#ifndef UTILS_H_
#define UTILS_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__has_include)
#if __has_include(<JaszczurHAL.h>)
#include <JaszczurHAL.h>
#elif __has_include("JaszczurHAL.h")
#include "JaszczurHAL.h"
#elif __has_include("../../../libraries/JaszczurHAL/src/JaszczurHAL.h")
#include "../../../libraries/JaszczurHAL/src/JaszczurHAL.h"
#else
#error "JaszczurHAL.h not found. Add JaszczurHAL to include paths."
#endif
#else
#include <JaszczurHAL.h>
#endif

/* AVR compatibility glue for font/lcd sources reused on RP2040. */
#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#endif

typedef unsigned char      u08;
typedef char               s08;
typedef unsigned short     u16;
typedef short              s16;
typedef unsigned long      u32;
typedef long               s32;
typedef unsigned long long u64;
typedef long long          s64;

typedef unsigned char      UCHAR;
typedef unsigned short     WORD;
typedef unsigned long      DWORD;
typedef char              *LPCTSTR;

void delay_ms(int ms);
int binatoi(char *s);
char *decToBinary(int n);
unsigned char BinToBCD(unsigned char bin);
unsigned char reverse_bits8(unsigned char b);
void doubleToDec(double val, int *hi, int *lo);

#endif /* UTILS_H_ */
