#ifndef T_HARDWARECONFIG
#define T_HARDWARECONFIG

#define SUPPORT_TRANSACTIONS

#define PWM_FREQUENCY_HZ 300
#define I2C_SPEED 50000

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

#define ADC_VOLT_PIN A2
#define ADC_SENSORS_PIN A1

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
#define HC4051_I_EGT 6
//7 not used ATM

#endif
