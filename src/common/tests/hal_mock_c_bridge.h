#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/* C access to the legacy C++ mock injection and log inspection helpers. */
void fiesta_test_adc_inject(uint8_t pin, int value);
void fiesta_test_set_micros(uint32_t value);
void fiesta_test_set_millis(uint32_t value);
void fiesta_test_usb_connected(bool connected);
const char *fiesta_test_debug_last_line(void);
#ifdef __cplusplus
}
#endif
