#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef HAL_ENABLE_CAN
/* Builds with CAN include the HAL configuration before this header. */
#include <hal/can/hal_can.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
/* C access to the legacy C++ mock injection and log inspection helpers. */
void fiesta_test_adc_inject(uint8_t pin, int value);
void fiesta_test_set_micros(uint32_t value);
void fiesta_test_set_millis(uint32_t value);
void fiesta_test_usb_connected(bool connected);
const char *fiesta_test_debug_last_line(void);
#ifdef HAL_ENABLE_CAN
void fiesta_test_can_reset(hal_can_t handle);
void fiesta_test_can_set_state(hal_can_t handle, hal_can_state_t state);
/* Oldest frame the channel sent: HAL_OK, HAL_EAGAIN when none, HAL_EINVAL. */
hal_status_t fiesta_test_can_pop_sent(hal_can_t handle, uint32_t *id,
                                      uint8_t *len, uint8_t *data);
#endif
#ifdef __cplusplus
}
#endif
