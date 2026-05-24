#ifndef DS18B20_H_
#define DS18B20_H_

#include <stdint.h>

/*
 * Compatibility API preserved for the original Fiesta firmware modules.
 * Implementation is backed by non-blocking hal_ds18b20 handles.
 */
void ds18b20_setPin(unsigned char pin);
void ds18b20_gettemp(int *a, int *b, double *native);

#endif


