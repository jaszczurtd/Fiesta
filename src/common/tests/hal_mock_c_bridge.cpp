#include <hal/impl/.mock/hal_mock.h>

#include "hal_mock_c_bridge.h"

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

#ifdef HAL_ENABLE_CAN
void fiesta_test_can_reset(hal_can_t handle) { hal_mock_can_reset(handle); }

void fiesta_test_can_set_state(hal_can_t handle, hal_can_state_t state) {
  hal_mock_can_set_state(handle, state);
}

hal_status_t fiesta_test_can_pop_sent(hal_can_t handle, uint32_t *id,
                                      uint8_t *len, uint8_t *data) {
  return hal_mock_can_get_sent(handle, id, len, data);
}
#endif
