#ifndef HARDWARECONFIG_H_
#define HARDWARECONFIG_H_

#include <stdint.h>
#include <stdbool.h>

/* Reserved pins */
#define PIN_MISO 0  /* Original AVR HW SPI MISO: PB4 (D12) */
#define PIN_MOSI 3  /* Original AVR HW SPI MOSI: PB3 (D11) */
#define PIN_SCK 2   /* Original AVR HW SPI SCK: PB5 (D13) */

#define CAN_INT 4   /* Not used in original Fiesta_clock */
#define CAN_CS 1    /* Not used in original Fiesta_clock */

/* Application pin mapping */
#define PIN_DS18B20_INSIDE 6   /* Original: PD2 (inside temp sensor) */
#define PIN_DS18B20_OUTSIDE 7  /* Original: PD3 (outside temp sensor) */

#define PIN_I2C_SDA 8          /* Original TWI SDA: PC4 (A4) */
#define PIN_I2C_SCL 9          /* Original TWI SCL: PC5 (A5) */

#define PIN_BUTTON_HOUR 10     /* Original: PD0 (increase hour) */
#define PIN_BUTTON_MINUTE 11   /* Original: PD1 (increase minute) */
#define PIN_BUTTON_SET 12      /* Original: PD4 (set/mode) */
#define PIN_IGNITION 13        /* Original: PC3 (ignition input) */

#define PIN_LED_RED 14         /* Original: PC0 */
#define PIN_LED_ORANGE 15      /* Original: PC1 */
#define PIN_LED_BLUE 16        /* Original: PC2 */

#define PIN_VOLT_ADC 26        /* Original ADC channel: ADC6 */

/* Bus and device config */
#define I2C_BUS_INDEX 0
#define I2C_CLOCK_HZ 100000UL

#define OLED_I2C_ADDR 0x3C
#define RTC_I2C_ADDR 0x51

/* Logic levels */
#define BUTTON_ACTIVE_LEVEL false
#define IGNITION_ACTIVE_LEVEL false
#define LED_ACTIVE_LEVEL true

/* Voltage divider constants */
#define VOLT_DIVIDER_R_TOP_OHMS 47000.0
#define VOLT_DIVIDER_R_BOTTOM_OHMS 3300.0

#endif /* HARDWARECONFIG_H_ */
