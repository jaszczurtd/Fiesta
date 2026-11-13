#include "hal_mock_c_bridge.h"
#include <hal/impl/.mock/hal_mock.h>

void fiesta_test_adc_inject(uint8_t pin, int value) {
  hal_mock_adc_inject(pin, value);
}

void fiesta_test_set_micros(uint32_t value) { hal_mock_set_micros(value); }
void fiesta_test_set_millis(uint32_t value) { hal_mock_set_millis(value); }
void fiesta_test_usb_connected(bool connected) {
  hal_mock_usb_set_connected(connected);
}

const char *fiesta_test_debug_last_line(void) {
  return hal_mock_deb_last_line();
}
