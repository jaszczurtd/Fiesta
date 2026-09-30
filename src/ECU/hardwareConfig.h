#ifndef T_HARDWARECONFIG
#define T_HARDWARECONFIG

#include "../common/adjustometer_protocol.h"
#include "../common/vp37/vp37_power_stage.h"
#include <libConfig.h>

#ifdef VP37
// 130 Hz: the current ripple at this frequency keeps the actuator free of
// the upper end stop and static friction on descents (bench, 2026-09-16);
#include "../common/vp37_drive_config.h"
#define TURBO_PWM_FREQUENCY_HZ 300
#define ANGLE_PWM_FREQUENCY_HZ VP37_TIMING_PWM_FREQUENCY_HZ
#else
#ifndef VP37_PWM_FREQUENCY_HZ
#define VP37_PWM_FREQUENCY_HZ 300
#endif
#define TURBO_PWM_FREQUENCY_HZ 300
#define ANGLE_PWM_FREQUENCY_HZ 300
#endif

// RP2040 flash-backed EEPROM emulation size used by ECU module.
#define ECU_EEPROM_SIZE_BYTES HAL_RP_FLASH_EEPROM_SIZE

#define PWM_WRITE_RESOLUTION VP37_PWM_WRITE_RESOLUTION
#define PWM_RESOLUTION VP37_PWM_RESOLUTION

// rpi pio pin numbers
#define PIO_INTERRUPT_HALL 7
#define PIO_TURBO 10
#define PIO_VP37_RPM VP37_QUANTITY_PWM_PIN
#define PIO_VP37_ANGLE VP37_TIMING_PWM_PIN

#define PIO_DPF_LAMP 8

#define A_4051 11
#define B_4051 12
#define C_4051 13

#define PIN_SDA 0
#define PIN_SCL 1

#define PIN_MISO 16
#define PIN_MOSI 19
#define PIN_SCK 18

#define ADC_VOLT_PIN VP37_SUPPLY_ADC_PIN
#define ADC_SENSORS_PIN VP37_SCAN_AUX_ADC_PIN
// The VP37 source shunt; no other peripheral may claim it.
#define ADC_VP37_CURRENT_PIN VP37_SHUNT_ADC_PIN

// for serial - GPS
#define SERIAL_RX_GPIO 22
#define SERIAL_TX_GPIO 21

// Set CS and INT for CAN
#define CAN0_GPIO 17
#define CAN0_INT 15

// Set CS and INT for OBD-2
#define CAN1_GPIO 6
#define CAN1_INT 14

// PCF8574 i2c addr
#define PCF8574_ADDR 0x38

// PCF8574 GPIO assignments
#define PCF8574_O_GLOW_PLUGS 0
#define PCF8574_O_FAN 1
#define PCF8574_O_HEATER_HI 2
#define PCF8574_O_HEATER_LO 3
#define PCF8574_O_GLOW_PLUGS_LAMP 4
#define PCF8574_O_HEATED_WINDOW_L 5
#define PCF8574_O_HEATED_WINDOW_P 6
#define PCF8574_O_VP37_ENABLE 7

// 4051 inputs assignments
#define HC4051_I_COOLANT_TEMP 0
#define HC4051_I_OIL_TEMP 1
#define HC4051_I_THROTTLE_POS 2
#define HC4051_I_AIR_TEMP 3
#define HC4051_I_FUEL_LEVEL 4
#define HC4051_I_BAR_PRESSURE 5
// 6 not used ATM
// 7 not used ATM

// physical pin of microcontroller for heated windows switch on/off
#define HEATED_WINDOWS_PIN 20

#define HAL_LED_PIN 25

// real values (resitance) for ECU main supply voltage measurement

#define V_DIVIDER_R1 VP37_SUPPLY_DIVIDER_R1
#define V_DIVIDER_R2 VP37_SUPPLY_DIVIDER_R2

// real values (resistance) for temperature (coolant/oil) measurement

#define R_TEMP_A 1506 // sensor
#define R_TEMP_B 1500

#define R_TEMP_AIR_A 5050 // sensor
#define R_TEMP_AIR_B 4800

// dividers - analog reads
#define DIVIDER_PRESSURE_BAR 955

#endif
