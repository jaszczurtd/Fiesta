#ifndef C_HARDWAREC
#define C_HARDWAREC

#define PIN_MISO 0
#define PIN_MOSI 3
#define PIN_SCK 2

#define CAN_INT 4
#define CAN_CS 1

#define PIN_RGB 16
#define NUMPIXELS 1

#define PWM_WRITE_RESOLUTION 11
#define PWM_RESOLUTION 2047

#define ABS_INPUT_PIN  14

// ADC input for resistive oil pressure sender (0..10 bar, nominal 10..180 Ohm)
#define OIL_PRESSURE_ADC_PIN A3

#define MCP9600_ADDR_PRE_DPF   0x60
#define MCP9600_ADDR_MID_DPF   0x67

#define PIN_I2C_SDA       12
#define PIN_I2C_SCL       13

#endif
