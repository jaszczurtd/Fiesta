#ifndef FIESTA_STATUS_LED_H
#define FIESTA_STATUS_LED_H

/**
 * @file fiesta_status_led.h
 * @brief The addressable status LED of an RP2040-Zero module, as the
 * statusLed of its device_tree.json describes it.
 */

#include <JaszczurHAL.h>
#include <string.h>

/**
 * @brief Colour order the assembly gives the status LED, for
 * hal_rgb_led_init_ex().
 *
 * The board profile allows "RGB" and "GRB"; anything but "RGB" is GRB.
 */
static inline hal_rgb_led_pixel_type_t fiesta_status_led_pixel_type(void) {
  return (strcmp(JH_HW_NODE_BOARD_STATUS_LED_PROP_PIXEL_ORDER, "RGB") == 0)
             ? HAL_RGB_LED_PIXEL_RGB_KHZ800
             : HAL_RGB_LED_PIXEL_GRB_KHZ800;
}

/** @brief Start the status LED in the colour order of the assembly. */
static inline hal_status_t fiesta_status_led_init(void) {
  return hal_rgb_led_init_ex(JH_HW_NODE_BOARD_STATUS_LED_PIN_DATA,
                             (uint8_t)JH_HW_NODE_BOARD_STATUS_LED_PROP_COUNT,
                             fiesta_status_led_pixel_type());
}

#endif
