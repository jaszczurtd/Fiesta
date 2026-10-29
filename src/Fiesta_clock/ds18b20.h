#ifndef DS18B20_H_
#define DS18B20_H_

#include <stdint.h>

/*
 * Compatibility API preserved for the original Fiesta firmware modules.
 * Implementation is backed by non-blocking hal_ds18b20 handles.
 */
void ds18b20_setPin(unsigned char pin);
void ds18b20_gettemp(int *a, int *b, double *native);

/** Progress the conversions of every sensor opened by ds18b20_setPin(). */
void ds18b20_poll(void);
/**
 * @brief Newest reading of the sensor opened on @p pin.
 * @return °C, or NAN when the sensor is not open or its newest conversion is
 *         @p maxAgeMs or more old.
 */
float ds18b20_fresh_temp(unsigned char pin, uint32_t maxAgeMs);

#endif
