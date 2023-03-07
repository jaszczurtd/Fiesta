#ifndef T_UTILS
#define T_UTILS

#include <Arduino.h>

// how many samples to take and average, more takes longer
// but is more 'smooth'
#define NUMSAMPLES 8

void deb(const char *format, ...);
int getAverageValueFrom(int tpin);
void floatToDec(float val, int *hi, int *lo);

#endif
