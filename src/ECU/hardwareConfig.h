#ifndef T_HARDWARECONFIG
#define T_HARDWARECONFIG

#include <libConfig.h>

#ifdef VP37
#define VP37_PWM_FREQUENCY_HZ 160
#define TURBO_PWM_FREQUENCY_HZ 300
#define ANGLE_PWM_FREQUENCY_HZ 200
#else
#define VP37_PWM_FREQUENCY_HZ 300
#define TURBO_PWM_FREQUENCY_HZ 300
#define ANGLE_PWM_FREQUENCY_HZ 300
#endif

#define I2C_SPEED_HZ 400000

// RP2040 flash-backed EEPROM emulation size used by ECU module.
#define ECU_EEPROM_SIZE_BYTES 2048u

#define PWM_WRITE_RESOLUTION 11
#define PWM_RESOLUTION 2047

//rpi pio pin numbers
#define PIO_INTERRUPT_HALL 7    
#define PIO_TURBO 10
#define PIO_VP37_RPM 9
#define PIO_VP37_ANGLE 5

#define PIO_DPF_LAMP 8

#define A_4051 11
#define B_4051 12
#define C_4051 13

#define PIN_SDA 0
#define PIN_SCL 1

#define PIN_MISO 16
#define PIN_MOSI 19
#define PIN_SCK 18

#define ADC_VOLT_PIN 28
#define ADC_SENSORS_PIN 27

//chip select pin for SD card reader
#define SD_CARD_CS 26

//for serial - GPS
#define SERIAL_RX_GPIO 22
#define SERIAL_TX_GPIO 21

// Set CS and INT for CAN 
#define CAN0_GPIO 17
#define CAN0_INT 15

// Set CS and INT for OBD-2 
#define CAN1_GPIO 6
#define CAN1_INT 14

// Adjustometer (VP37 feedback module) — I2C slave on shared bus
#define ADJUSTOMETER_I2C_ADDR      0x57
#define ADJUSTOMETER_REG_PULSE_HI  0x00  // int16 BE: frequency deviation from baseline [Hz]
#define ADJUSTOMETER_REG_PULSE_LO  0x01
#define ADJUSTOMETER_REG_VOLTAGE   0x02  // uint8: supply voltage in 0.1 V units
#define ADJUSTOMETER_REG_FUEL_TEMP 0x03  // uint8: fuel temperature °C
#define ADJUSTOMETER_REG_STATUS    0x04  // uint8: status bitmask
#define ADJUSTOMETER_REG_COUNT     5     // total bytes to read in one burst

// Adjustometer STATUS register bitmask
#define ADJ_STATUS_OK              0x00
#define ADJ_STATUS_SIGNAL_LOST     0x01
#define ADJ_STATUS_FUEL_TEMP_BROKEN 0x02
#define ADJ_STATUS_BASELINE_PENDING 0x04
#define ADJ_STATUS_VOLTAGE_BAD     0x08

// Maximum wait for Adjustometer baseline calibration at startup [ms].
// Must accommodate the oscillator warm-up period (ADJUSTOMETER_WARMUP_MS
// on the Adjustometer side) plus convergence (250 ms) plus post-convergence
// verification (1000 ms).  Extra margin handles repeated convergence restarts
// caused by slow oscillator drift on cold power-on.
#define ADJUSTOMETER_BASELINE_WAIT_MS 8000

//PCF8574 i2c addr
#define PCF8574_ADDR 0x38

//PCF8574 GPIO assignments
#define PCF8574_O_GLOW_PLUGS 0
#define PCF8574_O_FAN 1
#define PCF8574_O_HEATER_HI 2
#define PCF8574_O_HEATER_LO 3
#define PCF8574_O_GLOW_PLUGS_LAMP 4
#define PCF8574_O_HEATED_WINDOW_L 5
#define PCF8574_O_HEATED_WINDOW_P 6
#define PCF8574_O_VP37_ENABLE 7

//4051 inputs assignments 
#define HC4051_I_COOLANT_TEMP 0
#define HC4051_I_OIL_TEMP 1
#define HC4051_I_THROTTLE_POS 2
#define HC4051_I_AIR_TEMP 3
#define HC4051_I_FUEL_LEVEL 4
#define HC4051_I_BAR_PRESSURE 5
//6 not used ATM
//7 not used ATM

//physical pin of microcontroller for heated windows switch on/off
#define HEATED_WINDOWS_PIN 20

#define HAL_LED_PIN 25

//real values (resitance) for ECU main supply voltage measurement

#define V_DIVIDER_R1 49750.0
#define V_DIVIDER_R2 11800.0

//real values (resistance) for temperature (coolant/oil) measurement

#define R_TEMP_A 1506 //sensor
#define R_TEMP_B 1500

#define R_TEMP_AIR_A 5050 //sensor
#define R_TEMP_AIR_B 4800

//dividers - analog reads
#define DIVIDER_PRESSURE_BAR 955

#endif
