#ifndef HARDWARECONFIG_H_
#define HARDWARECONFIG_H_

#include <stdbool.h>
#include <stdint.h>

/* Pins, buses and addresses come from the rp2040-zero assembly of
 * device_tree.json; the code reads the JH_HW_* macros of the generated
 * jh_hardware.h directly. The blue LED on GP16 shares the data line of the
 * board's WS2812 (net in device_tree.json). */

/* hal_can_default_config() puts the MCP2515 on the default SPI bus 0. */
#if JH_HW_BUS_SPI_INDEX != 0
#error "Fiesta_clock drives the MCP2515 on SPI bus 0"
#endif

/* Logic levels, from activeLow of the nodes */
#define BUTTON_ACTIVE_LEVEL (JH_HW_NODE_BUTTON_SET_PROP_ACTIVE_LOW == 0)
#define IGNITION_ACTIVE_LEVEL (JH_HW_NODE_IGNITION_PROP_ACTIVE_LOW == 0)
#define LED_ACTIVE_LEVEL (JH_HW_NODE_LED_RED_PROP_ACTIVE_LOW == 0)
#if (JH_HW_NODE_BUTTON_HOUR_PROP_ACTIVE_LOW !=                                 \
     JH_HW_NODE_BUTTON_SET_PROP_ACTIVE_LOW) ||                                 \
    (JH_HW_NODE_BUTTON_MINUTE_PROP_ACTIVE_LOW !=                               \
     JH_HW_NODE_BUTTON_SET_PROP_ACTIVE_LOW)
#error "the buttons share one active level"
#endif
#if (JH_HW_NODE_LED_ORANGE_PROP_ACTIVE_LOW !=                                  \
     JH_HW_NODE_LED_RED_PROP_ACTIVE_LOW) ||                                    \
    (JH_HW_NODE_LED_BLUE_PROP_ACTIVE_LOW !=                                    \
     JH_HW_NODE_LED_RED_PROP_ACTIVE_LOW)
#error "the LEDs share one active level"
#endif

/* Voltage divider constants */
#define VOLT_DIVIDER_R_TOP_OHMS 47000.0
#define VOLT_DIVIDER_R_BOTTOM_OHMS 3300.0

#endif /* HARDWARECONFIG_H_ */
